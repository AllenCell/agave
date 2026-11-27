"""Verify the Vulkan files in a packaged macOS app or installed Python wheel."""

import argparse
import importlib.util
import json
import subprocess
from pathlib import Path


def verify(library_dir, manifest, consumer):
    driver = library_dir / "libvulkan_kosmickrisp.dylib"
    loader = library_dir / "libvulkan.1.dylib"
    assert driver.is_file(), driver
    assert loader.is_file(), loader
    assert manifest.is_file(), manifest
    metadata = json.loads(manifest.read_text())
    driver_path = Path(metadata["ICD"]["library_path"])
    assert not driver_path.is_absolute(), driver_path
    assert (manifest.parent / driver_path).resolve() == driver.resolve()
    assert not list(library_dir.glob("*MoltenVK*")), library_dir
    dependencies = subprocess.check_output(["otool", "-L", str(consumer)], text=True)
    assert "libvulkan.1.dylib" in dependencies, dependencies
    assert "libvulkan_kosmickrisp" not in dependencies, dependencies
    assert "MoltenVK" not in dependencies, dependencies
    for line in dependencies.splitlines()[1:]:
        dependency = line.strip().split(" (")[0]
        if "libvulkan" in dependency:
            assert dependency.startswith("@"), dependency
    print(f"Verified KosmicKrisp packaging: {manifest}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--app", type=Path, help="app bundle or CPack staging root")
    group.add_argument("--python-package", action="store_true")
    args = parser.parse_args()
    if args.app:
        bundles = (
            [args.app]
            if args.app.suffix == ".app"
            else list(args.app.rglob("agave.app"))
        )
        assert len(bundles) == 1, bundles
        contents = bundles[0] / "Contents"
        verify(
            contents / "Frameworks",
            contents / "Resources/vulkan/icd.d/libkosmickrisp_icd.json",
            contents / "MacOS/agave",
        )
    else:
        spec = importlib.util.find_spec("agave_pyvk")
        package_dir = Path(spec.origin).parent
        extensions = list(package_dir.glob("_native*.so"))
        assert len(extensions) == 1, extensions
        verify(
            package_dir,
            package_dir / "share/vulkan/icd.d/libkosmickrisp_icd.json",
            extensions[0],
        )
