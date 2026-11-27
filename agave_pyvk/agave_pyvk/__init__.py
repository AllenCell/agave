"""Headless in-process Vulkan bindings for AGAVE."""

import os
from pathlib import Path


_dll_directory_handle = None
if os.name == "nt":
    _dll_directory_handle = os.add_dll_directory(str(Path(__file__).resolve().parent))

from .agave import AgaveRenderer

__author__ = "Daniel Toloudis, Allen Institute"
__email__ = "danielt@alleninstitute.org"
__version__ = "1.9.0"


def get_module_version() -> str:
    return __version__


__all__ = ["AgaveRenderer", "get_module_version"]
