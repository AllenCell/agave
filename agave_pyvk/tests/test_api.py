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
    assert public_methods(pyvk) == public_methods(client)


def test_commands_are_forwarded_directly(monkeypatch):
    module = load_api(monkeypatch)
    renderer = module.AgaveRenderer(mode="raymarch")
    renderer.eye(1.0, 2.0, 3.0)
    assert renderer._renderer.calls[-1] == (3, (1.0, 2.0, 3.0))


def test_load_metadata_is_returned_as_dict(monkeypatch):
    module = load_api(monkeypatch)
    renderer = module.AgaveRenderer()
    assert renderer.load_data_and_get_info("volume.ome.tif") == {
        "commandId": 44,
        "x": 10,
    }
