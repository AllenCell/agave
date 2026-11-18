# -*- coding: utf-8 -*-

"""Top-level package for agave_pyclient."""

from .agave import AgaveRenderer  # noqa: F401

__author__ = "Daniel Toloudis, Allen Institute"
__email__ = "danielt@alleninstitute.org"
# Do not edit this string manually, always use bumpversion
# Details in CONTRIBUTING.md
__version__ = "2.0.0-dev.0"


def get_module_version() -> str:
    return __version__
