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

From the repository root, configure a persistent headless build and compile the
`agave_py2` nanobind target together with renderlib:

```console
cmake -S . -B build -DAGAVE_BUILD_APP=OFF -DAGAVE_BUILD_TESTS=OFF -DAGAVE_BUILD_PYVK=ON
cmake --build build --target agave_py2 --config Release --parallel
```

On Windows, add the vcpkg toolchain and Ninja Multi-Config arguments shown in
the root `README.md`, and run from a VS2026 x64 Native Tools Command Prompt.
The helper below reconfigures this CMake-owned tree for the active Python
interpreter; renderlib is rebuilt only when it is out of date.

For iterative development, install the staged package directly instead of
creating a wheel file. On macOS and Linux:

```console
python agave_pyvk/tools/build_native.py
AGAVE_PYVK_USE_PREBUILT=1 python -m pip install --force-reinstall --no-deps ./agave_pyvk
```

Or from a VS2026 x64 Native Tools Command Prompt on Windows:

```console
python agave_pyvk\tools\build_native.py
set "AGAVE_PYVK_USE_PREBUILT=1"
python -m pip install --force-reinstall --no-deps .\agave_pyvk
```

To produce a wheel file from the same staged build, install the `build` frontend,
set `AGAVE_PYVK_USE_PREBUILT=1` as above, and run:

```console
python -m pip install build
python -m build --wheel agave_pyvk
```
