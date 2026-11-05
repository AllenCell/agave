"""Headless in-process Vulkan bindings for AGAVE."""

import os
import sys
from pathlib import Path


_dll_directory_handle = None
if os.name == "nt":
    _dll_directory_handle = os.add_dll_directory(str(Path(__file__).resolve().parent))


def _check_native_abi() -> None:
    """Fail with a clear message if the staged _native extension is ABI-incompatible.

    The extension is built by CMake and staged into this package; there is no
    automatic rebuild when the active interpreter changes. Without this check,
    a mismatch surfaces as a misleading "circular import" ImportError.
    """
    pkg_dir = Path(__file__).resolve().parent
    current_tag = sys.implementation.cache_tag  # e.g. "cpython-314"
    if os.name == "nt":
        ext_glob, ext_suffix = "_native*.pyd", ".pyd"
    else:
        ext_glob, ext_suffix = "_native*.so", ".so"

    candidates = sorted(pkg_dir.glob(ext_glob))
    if not candidates:
        return  # let the normal import machinery raise ModuleNotFoundError

    def _tag(path: Path) -> str:
        # For example:
        # "_native.cpython-312-x86_64-linux-gnu.so"
        # -> "cpython-312-x86_64-linux-gnu"
        name = path.name[: -len(ext_suffix)]
        prefix = "_native."

        if not name.startswith(prefix):
            return ""

        return name[len(prefix):]

    current_tag = sys.implementation.cache_tag

    def is_compatible(path: Path) -> bool:
        tag = _tag(path)
        return tag == current_tag or tag.startswith(f"{current_tag}-")

    if any(is_compatible(path) for path in candidates):
        return

    found = ", ".join(sorted({_tag(p) or p.name for p in candidates}))
    raise ImportError(
        f"agave_pyvk native extension ABI mismatch: found [{found}] in {pkg_dir}, "
        f"but current interpreter is {current_tag} ({sys.executable}). "
        "Reconfigure the CMake build with "
        "-DPython_EXECUTABLE=$(python -c 'import sys; print(sys.executable)') "
        "and rebuild the stage_agave_pyvk target."
    )


_check_native_abi()

from .agave import AgaveRenderer

__author__ = "Daniel Toloudis, Allen Institute"
__email__ = "danielt@alleninstitute.org"
__version__ = "1.9.0"


def get_module_version() -> str:
    return __version__


__all__ = ["AgaveRenderer", "get_module_version"]
