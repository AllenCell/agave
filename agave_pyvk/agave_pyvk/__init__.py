"""Headless in-process Vulkan bindings for AGAVE."""

from .agave import AgaveRenderer

__author__ = "Daniel Toloudis, Allen Institute"
__email__ = "danielt@alleninstitute.org"
__version__ = "1.9.0"


def get_module_version() -> str:
    return __version__


__all__ = ["AgaveRenderer", "get_module_version"]
