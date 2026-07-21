"""Setuptools backend that builds only the Python-specific CMake target."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from setuptools import build_meta as _setuptools


_PROJECT_DIR = Path(__file__).resolve().parent
_SOURCE_DIR = _PROJECT_DIR.parent


def _cmake_settings() -> tuple[Path, str]:
    marker = _PROJECT_DIR / ".cmake-build-dir"
    if not marker.is_file():
        raise RuntimeError(
            "No top-level AGAVE CMake build was found. Configure and build "
            "AGAVE before installing agave_pyvk."
        )

    marker_lines = marker.read_text(encoding="utf-8").splitlines()
    if not marker_lines or not marker_lines[0]:
        raise RuntimeError(f"Invalid top-level CMake build marker: {marker}")

    build_dir = Path(marker_lines[0]).resolve()
    build_type = marker_lines[1] if len(marker_lines) > 1 else ""
    return build_dir, build_type


def _build_native() -> None:
    build_dir, build_type = _cmake_settings()
    subprocess.run(
        [
            "cmake",
            "-S",
            str(_SOURCE_DIR),
            "-B",
            str(build_dir),
            "-UPython_*",
            "-DAGAVE_BUILD_PYVK=ON",
            f"-DPython_EXECUTABLE={sys.executable}",
            f"-DPython_ROOT_DIR={sys.prefix}",
        ],
        check=True,
    )
    build_command = [
        "cmake",
        "--build",
        str(build_dir),
        "--target",
        "stage_agave_pyvk",
    ]
    if build_type:
        build_command.extend(["--config", build_type])
    build_command.append("--parallel")
    subprocess.run(build_command, check=True)


def build_wheel(wheel_directory, config_settings=None, metadata_directory=None):
    _build_native()
    return _setuptools.build_wheel(wheel_directory, config_settings, metadata_directory)


def build_editable(wheel_directory, config_settings=None, metadata_directory=None):
    _build_native()
    return _setuptools.build_editable(
        wheel_directory, config_settings, metadata_directory
    )


build_sdist = _setuptools.build_sdist
get_requires_for_build_wheel = _setuptools.get_requires_for_build_wheel
get_requires_for_build_sdist = _setuptools.get_requires_for_build_sdist
get_requires_for_build_editable = _setuptools.get_requires_for_build_editable
prepare_metadata_for_build_wheel = _setuptools.prepare_metadata_for_build_wheel
prepare_metadata_for_build_editable = _setuptools.prepare_metadata_for_build_editable
