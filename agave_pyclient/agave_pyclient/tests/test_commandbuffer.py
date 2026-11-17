"""Basic tests for agave_pyclient.commandbuffer.CommandBuffer.

Also includes consistency checks ensuring every command declared in
``renderlib/command.h`` (the source of truth on the C++ side) is mirrored
both in :data:`agave_pyclient.commandbuffer.COMMANDS` and as a method on
:class:`agave_pyclient.agave.AgaveRenderer`.
"""

import os
import re
import struct

import pytest

from agave_pyclient.commandbuffer import COMMANDS, CommandBuffer

# ---------------------------------------------------------------------------
# CommandBuffer basics
# ---------------------------------------------------------------------------


def test_empty_buffer_has_zero_size():
    cb = CommandBuffer()
    assert cb.compute_size() == 0
    assert bytes(cb.make_buffer()) == b""


def test_add_command_appends_to_prebuffer():
    cb = CommandBuffer()
    cb.add_command("REDRAW")
    cb.add_command("APERTURE", 0.5)
    assert cb.prebuffer == [("REDRAW",), ("APERTURE", 0.5)]


def test_constructor_accepts_command_list():
    cb = CommandBuffer([["REDRAW"], ["APERTURE", 0.5]])
    assert cb.prebuffer == [("REDRAW",), ("APERTURE", 0.5)]


# ---------------------------------------------------------------------------
# Buffer size computation
# ---------------------------------------------------------------------------


def test_compute_size_no_args():
    cb = CommandBuffer()
    cb.add_command("REDRAW")
    assert cb.compute_size() == 4


def test_compute_size_scalar_args():
    cb = CommandBuffer()
    cb.add_command("EYE", 1.0, 2.0, 3.0)
    assert cb.compute_size() == 4 + 3 * 4


def test_compute_size_string_arg():
    cb = CommandBuffer()
    cb.add_command("SESSION", "hi")
    assert cb.compute_size() == 4 + 4 + 2


def test_compute_size_array_arg():
    cb = CommandBuffer()
    cb.add_command("SET_CONTROL_POINTS", 0, [0.0, 1.0, 2.0, 3.0])
    assert cb.compute_size() == 4 + 4 + 4 + 4 * 4


def test_compute_size_multiple_commands():
    cb = CommandBuffer()
    cb.add_command("REDRAW")
    cb.add_command("EYE", 1.0, 2.0, 3.0)
    cb.add_command("SESSION", "abc")
    assert cb.compute_size() == 4 + (4 + 12) + (4 + 4 + 3)


# ---------------------------------------------------------------------------
# Buffer encoding
# ---------------------------------------------------------------------------


def test_make_buffer_encodes_command_id_big_endian():
    cb = CommandBuffer()
    cb.add_command("REDRAW")
    buf = cb.make_buffer()
    assert len(buf) == 4
    assert struct.unpack_from(">i", buf, 0)[0] == COMMANDS["REDRAW"][0]


def test_make_buffer_encodes_int32_args_big_endian():
    cb = CommandBuffer()
    cb.add_command("SET_TIME", 42)
    buf = bytes(cb.make_buffer())
    assert struct.unpack(">ii", buf) == (COMMANDS["SET_TIME"][0], 42)


def test_make_buffer_encodes_float_args():
    cb = CommandBuffer()
    cb.add_command("APERTURE", 0.5)
    buf = bytes(cb.make_buffer())
    cmd_id = struct.unpack_from(">i", buf, 0)[0]
    aperture = struct.unpack_from("f", buf, 4)[0]
    assert cmd_id == COMMANDS["APERTURE"][0]
    assert aperture == pytest.approx(0.5)


def test_make_buffer_encodes_string_arg():
    cb = CommandBuffer()
    cb.add_command("SESSION", "abc")
    buf = bytes(cb.make_buffer())
    cmd_id, length = struct.unpack_from(">ii", buf, 0)
    assert cmd_id == COMMANDS["SESSION"][0]
    assert length == 3
    assert buf[8:] == b"abc"


def test_make_buffer_encodes_f32_array():
    cb = CommandBuffer()
    cb.add_command("SET_CONTROL_POINTS", 1, [0.25, 0.75])
    buf = bytes(cb.make_buffer())
    cmd_id, channel, arr_len = struct.unpack_from(">iii", buf, 0)
    f0 = struct.unpack_from("f", buf, 12)[0]
    f1 = struct.unpack_from("f", buf, 16)[0]
    assert cmd_id == COMMANDS["SET_CONTROL_POINTS"][0]
    assert channel == 1
    assert arr_len == 2
    assert f0 == pytest.approx(0.25)
    assert f1 == pytest.approx(0.75)


def test_make_buffer_encodes_i32_array():
    cb = CommandBuffer()
    cb.add_command("LOAD_DATA", "x", 0, 0, 0, [1, 2], [3, 4, 5])
    buf = bytes(cb.make_buffer())

    cmd_id = struct.unpack_from(">i", buf, 0)[0]
    assert cmd_id == COMMANDS["LOAD_DATA"][0]

    offset = 4
    str_len = struct.unpack_from(">i", buf, offset)[0]
    assert str_len == 1
    offset += 4 + str_len

    a, b, c = struct.unpack_from(">iii", buf, offset)
    assert (a, b, c) == (0, 0, 0)
    offset += 12

    arr_len = struct.unpack_from(">i", buf, offset)[0]
    assert arr_len == 2
    offset += 4
    assert struct.unpack_from(">ii", buf, offset) == (1, 2)
    offset += 8

    arr_len = struct.unpack_from(">i", buf, offset)[0]
    assert arr_len == 3
    offset += 4
    assert struct.unpack_from(">iii", buf, offset) == (3, 4, 5)
    offset += 12

    assert offset == len(buf)


def test_make_buffer_multiple_commands_concatenated():
    cb = CommandBuffer()
    cb.add_command("REDRAW")
    cb.add_command("APERTURE", 1.5)
    buf = bytes(cb.make_buffer())
    assert len(buf) == 4 + (4 + 4)
    assert struct.unpack_from(">i", buf, 0)[0] == COMMANDS["REDRAW"][0]
    assert struct.unpack_from(">i", buf, 4)[0] == COMMANDS["APERTURE"][0]
    assert struct.unpack_from("f", buf, 8)[0] == pytest.approx(1.5)


# ---------------------------------------------------------------------------
# Error handling
# ---------------------------------------------------------------------------


def test_make_buffer_unknown_command_raises():
    cb = CommandBuffer()
    cb.prebuffer.append(("NOT_A_REAL_COMMAND",))
    with pytest.raises(KeyError):
        cb.make_buffer()


def test_compute_size_wrong_arg_count_returns_zero(capsys):
    cb = CommandBuffer()
    cb.add_command("EYE", 1.0, 2.0)  # missing third float
    assert cb.compute_size() == 0
    out = capsys.readouterr().out
    assert "BAD COMMAND" in out


# ---------------------------------------------------------------------------
# Cross-language consistency: renderlib/command.h ⇄ commandbuffer.COMMANDS
# ---------------------------------------------------------------------------


# Maps the C++ ``CommandArgType`` enum to the short codes used by COMMANDS.
_CPP_TO_PY_ARG = {
    "STR": "S",
    "F32": "F32",
    "I32": "I32",
    "F32A": "F32A",
    "I32A": "I32A",
}

# Matches a CMDDECL invocation (single- or multi-line). After normalising
# whitespace the form is: ``CMDDECL( ClassName , <id> , "python_name" ,
# CMD_ARGS({ CommandArgType::X, ... }))``
_CMDDECL_RE = re.compile(
    r'CMDDECL\(\s*\w+\s*,\s*(\d+)\s*,\s*"(\w+)"\s*,'
    r"\s*CMD_ARGS\(\s*\{([^}]*)\}\s*\)",
)


def _find_command_h():
    """Return the path to renderlib/command.h, or None if running from an
    installed package outside the source tree."""
    here = os.path.dirname(os.path.abspath(__file__))
    candidate = os.path.normpath(
        os.path.join(here, "..", "..", "..", "renderlib", "command.h")
    )
    return candidate if os.path.isfile(candidate) else None


def _parse_cpp_commands():
    """Parse CMDDECL() declarations from renderlib/command.h.

    Returns a dict keyed by integer command id with value
    ``(python_name, [arg_codes])``. Skips the #define line by virtue of the
    ``(\\d+)`` capture on the second positional argument.
    """
    path = _find_command_h()
    if path is None:
        pytest.skip("renderlib/command.h not available (running outside source tree)")

    with open(path, "r") as f:
        text = f.read()

    # Collapse newlines so multi-line CMDDECLs match the same regex.
    flat = re.sub(r"\s+", " ", text)

    commands = {}
    for m in _CMDDECL_RE.finditer(flat):
        cmd_id = int(m.group(1))
        py_name = m.group(2)
        args_blob = m.group(3)
        arg_codes = []
        for arg in re.findall(r"CommandArgType::(\w+)", args_blob):
            assert arg in _CPP_TO_PY_ARG, (
                f"Unknown CommandArgType::{arg} in renderlib/command.h "
                f"for command id {cmd_id} ({py_name}); please update the "
                "_CPP_TO_PY_ARG mapping in this test."
            )
            arg_codes.append(_CPP_TO_PY_ARG[arg])
        commands[cmd_id] = (py_name, arg_codes)
    return commands


@pytest.fixture(scope="module")
def cpp_commands():
    cmds = _parse_cpp_commands()
    assert cmds, "Failed to parse any CMDDECL entries from renderlib/command.h"
    return cmds


@pytest.fixture(scope="module")
def py_commands_by_id():
    """Invert COMMANDS to a dict keyed by command id."""
    by_id = {}
    for key, sig in COMMANDS.items():
        cmd_id = sig[0]
        arg_codes = list(sig[1:])
        assert cmd_id not in by_id, (
            f"Duplicate command id {cmd_id} in COMMANDS "
            f"({by_id[cmd_id][0]!r} vs {key!r})"
        )
        by_id[cmd_id] = (key, arg_codes)
    return by_id


def test_every_cpp_command_is_in_python_COMMANDS(cpp_commands, py_commands_by_id):
    """For every CMDDECL in command.h, COMMANDS must have an entry with the
    same id and arg type list."""
    missing = []
    mismatched_args = []
    for cmd_id, (py_name, arg_codes) in sorted(cpp_commands.items()):
        if cmd_id not in py_commands_by_id:
            missing.append((cmd_id, py_name))
            continue
        py_key, py_args = py_commands_by_id[cmd_id]
        if py_args != arg_codes:
            mismatched_args.append(
                f"id {cmd_id} ({py_name}/{py_key}): C++={arg_codes} Python={py_args}"
            )

    assert not missing, (
        "Commands declared in renderlib/command.h but missing from "
        f"commandbuffer.COMMANDS: {missing}"
    )
    assert not mismatched_args, (
        "Argument-type mismatches between command.h and commandbuffer.COMMANDS:\n  "
        + "\n  ".join(mismatched_args)
    )


def test_no_extra_ids_in_python_COMMANDS(cpp_commands, py_commands_by_id):
    """COMMANDS must not contain ids that aren't declared in command.h."""
    extra = sorted(set(py_commands_by_id) - set(cpp_commands))
    assert not extra, (
        "commandbuffer.COMMANDS has ids not declared in renderlib/command.h: "
        f"{[(i, py_commands_by_id[i][0]) for i in extra]}"
    )


def test_every_cpp_command_has_agave_pyer_method(cpp_commands):
    """Every C++ python_name must exist as a method on AgaveRenderer."""
    pytest.importorskip("ws4py")  # agave.py imports ws4py at module load
    from agave_pyclient.agave import AgaveRenderer

    missing = []
    for cmd_id, (py_name, _) in sorted(cpp_commands.items()):
        attr = getattr(AgaveRenderer, py_name, None)
        if attr is None or not callable(attr):
            missing.append((cmd_id, py_name))

    assert not missing, (
        "Commands declared in renderlib/command.h are missing matching "
        f"methods on AgaveRenderer: {missing}"
    )
