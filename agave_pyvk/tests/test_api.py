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

    def close(self):
        self.closed = True


def load_api(monkeypatch):
    fake_native = types.ModuleType("agave_pyvk._native")
    fake_native.__file__ = str(Path(__file__).parent / "_native.pyd")
    fake_native.PythonRenderer = FakePythonRenderer
    monkeypatch.setitem(sys.modules, "agave_pyvk._native", fake_native)
    sys.modules.pop("agave_pyvk.agave", None)
    return importlib.import_module("agave_pyvk.agave")


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
    pyvk = repository / "agave_pyvk" / "agave_pyvk" / "agave.py"
    pyclient_methods = public_methods(client)
    pyvk_methods = public_methods(pyvk)
    assert pyclient_methods.issubset(pyvk_methods)
    assert pyvk_methods - pyclient_methods == {"load_array"}


def test_commands_are_forwarded_directly(monkeypatch):
    module = load_api(monkeypatch)
    renderer = module.AgaveRenderer(mode="raymarch")
    renderer.eye(1.0, 2.0, 3.0)
    assert renderer._renderer.calls[-1] == (3, (1.0, 2.0, 3.0))


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
