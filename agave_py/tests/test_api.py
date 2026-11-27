import ast
import importlib
import json
import sys
import types
from pathlib import Path


class FakePythonRenderer:
    def __init__(self, mode, asset_path, gpu):
        self.constructor_args = (mode, asset_path, gpu)
        self.calls = []
        self.closed = False

    def execute(self, command_id, *args):
        self.calls.append((command_id, args))
        if command_id == 44:
            return json.dumps({"commandId": 44, "x": 10})
        if command_id == 15:
            return (1, 1, bytes((0, 0, 255, 255)))
        return None

    def load_array(self, data, name, voxel_size, spatial_units, channel_names):
        self.calls.append(
            ("load_array", (data, name, voxel_size, spatial_units, channel_names))
        )
        return json.dumps(
            {
                "name": name,
                "x": data.shape[-1],
                "y": data.shape[-2],
                "z": data.shape[-3],
                "c": data.shape[0] if data.ndim == 4 else 1,
            }
        )

    def set_render_mode(self, mode):
        self.calls.append(("set_render_mode", (mode,)))

    def set_slice_indices(self, x, y, z):
        self.calls.append(("set_slice_indices", (x, y, z)))

    def close(self):
        self.closed = True


def load_api(monkeypatch):
    fake_native = types.ModuleType("agave_py._native")
    fake_native.__file__ = str(Path(__file__).parent / "_native.pyd")
    fake_native.PythonRenderer = FakePythonRenderer
    monkeypatch.setitem(sys.modules, "agave_py._native", fake_native)
    module = importlib.import_module("agave_py.agave")
    monkeypatch.setattr(module, "_native", fake_native)
    return module


def public_methods(path):
    tree = ast.parse(path.read_text(encoding="utf-8"))
    cls = next(
        node
        for node in tree.body
        if isinstance(node, ast.ClassDef) and node.name == "AgaveRenderer"
    )
    return {
        node.name
        for node in cls.body
        if isinstance(node, ast.FunctionDef) and not node.name.startswith("_")
    }


def test_public_api_matches_pyclient():
    repository = Path(__file__).resolve().parents[2]
    client = repository / "agave_pyclient" / "agave_pyclient" / "agave.py"
    native = repository / "agave_py" / "agave_py" / "agave.py"
    pyclient_methods = public_methods(client)
    native_methods = public_methods(native)
    assert pyclient_methods.issubset(native_methods)
    assert native_methods - pyclient_methods == {
        "load_array",
        "render_frame",
        "set_render_mode",
        "set_slice_indices",
    }


def test_multiple_renderers_have_independent_frames_and_gpu_selection(monkeypatch):
    import numpy as np

    module = load_api(monkeypatch)
    first = module.AgaveRenderer(mode="raymarch", gpu=2)
    second = module.AgaveRenderer(mode="pathtrace", gpu=2)
    assert first._renderer is not second._renderer
    assert first._renderer.constructor_args[::2] == ("raymarch", 2)
    assert second._renderer.constructor_args[::2] == ("pathtrace", 2)
    frame = first.render_frame()
    assert frame.shape == (1, 1, 4)
    assert frame.dtype == np.uint8
    assert not frame.flags.writeable
    first.close()
    assert not second._renderer.closed
    assert second.render_frame().shape == (1, 1, 4)


def test_canvas_loads_its_own_volume_and_resolution(monkeypatch):
    import numpy as np

    module = load_api(monkeypatch)
    volume_a = np.zeros((2, 3, 4), dtype=np.uint16)
    volume_b = np.ones((2, 3, 4), dtype=np.uint16)
    first = module.AgaveCanvas(volume_a, mode="raymarch", width=320, height=240)
    second = module.AgaveCanvas(volume_b, mode="pathtrace", width=640, height=480)
    assert first._renderer is not second._renderer
    assert first._renderer.calls[0][1][0] is volume_a
    assert second._renderer.calls[0][1][0] is volume_b
    assert first._renderer.calls[1] == (16, (320, 240))
    assert second._renderer.calls[1] == (16, (640, 480))


def test_commands_are_forwarded_directly(monkeypatch):
    module = load_api(monkeypatch)
    renderer = module.AgaveRenderer(mode="raymarch")
    renderer.eye(1.0, 2.0, 3.0)
    assert renderer._renderer.calls[-1] == (3, (1.0, 2.0, 3.0))


def test_set_render_mode_is_forwarded_and_rejected_when_closed(monkeypatch):
    import pytest

    module = load_api(monkeypatch)
    renderer = module.AgaveRenderer(mode="raymarch")
    native = renderer._renderer
    renderer.set_render_mode("pathtrace")
    assert native.calls[-1] == ("set_render_mode", ("pathtrace",))
    renderer.close()
    with pytest.raises(RuntimeError, match="closed"):
        renderer.set_render_mode("raymarch")


def test_slice_modes_reach_the_native_renderer(monkeypatch):
    module = load_api(monkeypatch)
    for mode in ("slice_z", "slice_y", "slice_x", "triple"):
        renderer = module.AgaveRenderer(mode=mode)
        assert renderer._renderer.constructor_args[0] == mode
    renderer.set_slice_indices(1, 2, 3)
    assert renderer._renderer.calls[-1] == ("set_slice_indices", (1, 2, 3))


def test_multichannel_blend_is_forwarded(monkeypatch):
    module = load_api(monkeypatch)
    renderer = module.AgaveRenderer()
    renderer.set_multichannel_blend(1)
    assert renderer._renderer.calls[-1] == (54, (1,))


def test_load_metadata_is_returned_as_dict(monkeypatch):
    module = load_api(monkeypatch)
    renderer = module.AgaveRenderer()
    assert renderer.load_data_and_get_info("volume.ome.tif") == {
        "commandId": 44,
        "x": 10,
    }


def test_load_array_forwards_contiguous_data_and_metadata(monkeypatch):
    import numpy as np

    module = load_api(monkeypatch)
    renderer = module.AgaveRenderer()
    data = np.arange(24, dtype=np.uint16).reshape(2, 3, 4)[:, :, ::-1]

    result = renderer.load_array(
        data,
        name="cells",
        voxel_size=(0.5, 0.6, 0.7),
        spatial_units="um",
        channel_names=["DNA"],
    )

    _, args = renderer._renderer.calls[-1]
    forwarded, name, voxel_size, units, channel_names = args
    assert forwarded.flags.c_contiguous
    assert np.array_equal(forwarded, data)
    assert (name, voxel_size, units, channel_names) == (
        "cells",
        [0.5, 0.6, 0.7],
        "um",
        ["DNA"],
    )
    assert result == {"name": "cells", "x": 4, "y": 3, "z": 2, "c": 1}


def test_load_array_rejects_unsupported_dtype_before_native_call(monkeypatch):
    import numpy as np
    import pytest

    module = load_api(monkeypatch)
    renderer = module.AgaveRenderer()
    with pytest.raises(TypeError, match="dtype"):
        renderer.load_array(np.zeros((2, 3, 4), dtype=np.int32))
