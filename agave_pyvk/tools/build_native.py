"""Build and stage agave_py2 using a persistent, CMake-owned build tree."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]


def _configured_path(name: str, default: Path) -> Path:
    value = os.environ.get(name)
    return Path(value).resolve() if value else default.resolve()


def _run(command: list[str]) -> None:
    subprocess.run(command, check=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--stage-only",
        action="store_true",
        help="stage an agave_py2 target already built by CMake",
    )
    args = parser.parse_args()

    if not args.stage_only and os.name == "nt" and "VSCMD_VER" not in os.environ:
        raise RuntimeError(
            "build_native.py must run from an MSVC x64 Native Tools prompt"
        )

    build_dir = _configured_path(
        "AGAVE_PYVK_CMAKE_BUILD_DIR", PROJECT_ROOT / "build"
    )
    stage_dir = _configured_path(
        "AGAVE_PYVK_PREBUILT_DIR", build_dir / "agave-pyvk-stage"
    )

    if not args.stage_only:
        _run(
            [
                "cmake",
                "-S",
                str(PROJECT_ROOT),
                "-B",
                str(build_dir),
                "-USKBUILD_*",
                "-Unanobind_DIR",
                "-UPython_*",
                "-DAGAVE_BUILD_APP=OFF",
                "-DAGAVE_BUILD_TESTS=OFF",
                "-DAGAVE_BUILD_PYVK=ON",
                f"-DPython_EXECUTABLE={sys.executable}",
                f"-DPython_ROOT_DIR={sys.prefix}",
            ]
        )
        _run(
            [
                "cmake",
                "--build",
                str(build_dir),
                "--target",
                "agave_py2",
                "--config",
                "Release",
                "--parallel",
            ]
        )

    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    _run(
        [
            "cmake",
            "--install",
            str(build_dir),
            "--config",
            "Release",
            "--component",
            "agave_pyvk",
            "--prefix",
            str(stage_dir),
        ]
    )


if __name__ == "__main__":
    main()
