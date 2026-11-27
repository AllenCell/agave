import importlib.util
import os
import sys
from pathlib import Path

import pytest


@pytest.mark.skipif(os.name != "nt", reason="Windows extension suffixes differ")
@pytest.mark.parametrize("matching", [True, False])
def test_windows_native_abi_check_accepts_only_current_python(
    tmp_path, monkeypatch, matching
):
    package_dir = tmp_path / "abi_probe"
    package_dir.mkdir()
    source_init = Path(__file__).resolve().parents[1] / "agave_py" / "__init__.py"
    (package_dir / "__init__.py").write_bytes(source_init.read_bytes())
    (package_dir / "agave.py").write_text(
        "class AgaveRenderer: pass\nclass AgaveCanvas: pass\n"
    )

    minor = sys.version_info.minor if matching else sys.version_info.minor - 1
    (package_dir / f"_native.cp{sys.version_info.major}{minor}-win_amd64.pyd").touch()

    module_name = f"abi_probe_{'matching' if matching else 'mismatched'}"
    spec = importlib.util.spec_from_file_location(
        module_name,
        package_dir / "__init__.py",
        submodule_search_locations=[str(package_dir)],
    )
    module = importlib.util.module_from_spec(spec)
    monkeypatch.setitem(sys.modules, module_name, module)

    if matching:
        spec.loader.exec_module(module)
    else:
        with pytest.raises(ImportError, match="native extension ABI mismatch"):
            spec.loader.exec_module(module)
