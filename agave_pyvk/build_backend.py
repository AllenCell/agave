"""PEP 517 backend supporting wheels staged by the shared CMake build."""

from __future__ import annotations

import os
from email.message import Message
from pathlib import Path

from packaging.tags import sys_tags
from scikit_build_core import build as _scikit_build
from wheel.wheelfile import WheelFile

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - Python 3.10
    import tomli as tomllib


_PROJECT_ROOT = Path(__file__).resolve().parent


def _project_metadata() -> dict:
    with (_PROJECT_ROOT / "pyproject.toml").open("rb") as stream:
        return tomllib.load(stream)["project"]


def _metadata_text(project: dict) -> str:
    metadata = Message()
    metadata["Metadata-Version"] = "2.1"
    metadata["Name"] = project["name"]
    metadata["Version"] = project["version"]
    metadata["Summary"] = project["description"]
    metadata["Requires-Python"] = project["requires-python"]
    for author in project.get("authors", []):
        metadata["Author"] = author["name"]
        metadata["Author-email"] = author["email"]
    for dependency in project.get("dependencies", []):
        metadata["Requires-Dist"] = dependency
    metadata.set_payload("")
    return metadata.as_string()


def _prebuilt_directory() -> Path:
    configured = os.environ.get("AGAVE_PYVK_PREBUILT_DIR")
    if configured:
        return Path(configured).resolve()
    return (_PROJECT_ROOT.parent / "build" / "agave-pyvk-stage").resolve()


def _build_prebuilt_wheel(wheel_directory: str) -> str:
    package_stage = _prebuilt_directory() / "agave_pyvk"
    if not package_stage.is_dir():
        raise RuntimeError(f"staged package directory does not exist: {package_stage}")

    current_tag = next(sys_tags())
    tag = f"{current_tag.interpreter}-{current_tag.abi}-{current_tag.platform}"
    project = _project_metadata()
    distribution = project["name"].replace("-", "_")
    version = project["version"]
    wheel_name = f"{distribution}-{version}-{tag}.whl"
    dist_info = f"{distribution}-{version}.dist-info"
    output = Path(wheel_directory).resolve()
    output.mkdir(parents=True, exist_ok=True)

    package_files = {}
    for source_root in (_PROJECT_ROOT / "agave_pyvk", package_stage):
        for source in source_root.rglob("*"):
            if source.is_file() and "__pycache__" not in source.parts:
                archive_path = Path("agave_pyvk") / source.relative_to(source_root)
                package_files[archive_path.as_posix()] = source

    wheel_metadata = (
        "Wheel-Version: 1.0\n"
        "Generator: agave_pyvk prebuilt backend\n"
        "Root-Is-Purelib: false\n"
        f"Tag: {tag}\n"
    )
    with WheelFile(output / wheel_name, "w") as wheel:
        for archive_path, source in sorted(package_files.items()):
            wheel.write(source, archive_path)
        wheel.writestr(f"{dist_info}/METADATA", _metadata_text(project))
        wheel.writestr(f"{dist_info}/WHEEL", wheel_metadata)
    return wheel_name


def build_wheel(
    wheel_directory: str,
    config_settings=None,
    metadata_directory: str | None = None,
) -> str:
    if os.environ.get("AGAVE_PYVK_USE_PREBUILT") == "1":
        return _build_prebuilt_wheel(wheel_directory)
    return _scikit_build.build_wheel(
        wheel_directory, config_settings, metadata_directory
    )


build_sdist = _scikit_build.build_sdist
build_editable = _scikit_build.build_editable
get_requires_for_build_wheel = _scikit_build.get_requires_for_build_wheel
get_requires_for_build_sdist = _scikit_build.get_requires_for_build_sdist
get_requires_for_build_editable = _scikit_build.get_requires_for_build_editable
prepare_metadata_for_build_wheel = _scikit_build.prepare_metadata_for_build_wheel
prepare_metadata_for_build_editable = _scikit_build.prepare_metadata_for_build_editable
