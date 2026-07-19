# agave_pyvk

`agave_pyvk` is the standalone, headless, in-process Vulkan Python package for
AGAVE. Its `AgaveRenderer` API mirrors `agave_pyclient`, but commands cross a
nanobind boundary and execute synchronously in renderlib. It has no Qt or
WebSocket dependency.

```python
from agave_pyvk import AgaveRenderer

with AgaveRenderer(mode="pathtrace") as renderer:
    info = renderer.load_data_and_get_info("image.ome.tif")
    renderer.set_resolution(1024, 1024)
    renderer.session("render.png")
    renderer.redraw()
```

A Vulkan 1.3-capable driver and the Vulkan SDK are required to build. The
remaining native dependencies are the same ones used by renderlib.

## Reusing a local CMake build

Configure the normal AGAVE build with `AGAVE_BUILD_PYVK=ON`. The regular build
then compiles renderlib and the `agave_py2` nanobind target together:

```powershell
cmake -S . -B D:\agave_build -DAGAVE_BUILD_PYVK=ON
cmake --build D:\agave_build --config Release
```

To create a wheel without compiling renderlib again, have the package helper
build the Python-specific target in that tree and stage its install component:

```powershell
$env:AGAVE_PYVK_CMAKE_BUILD_DIR = "D:\agave_build"
$env:AGAVE_PYVK_PREBUILT_DIR = "D:\agave_build\agave-pyvk-stage"
python agave_pyvk\tools\build_native.py

$env:AGAVE_PYVK_USE_PREBUILT = "1"
python -m pip install build
python -m build --wheel agave_pyvk
```

The helper reconfigures the CMake-owned tree for the active Python interpreter.
Renderlib remains up to date; only the Python-version-specific nanobind sources
need to be rebuilt when the interpreter ABI changes. Run it from the same MSVC
x64 Native Tools prompt used for the CMake build on Windows.
