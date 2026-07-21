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

## Editable development install

Configure the normal top-level AGAVE build once. Then build renderlib, the AGAVE
application, and the `agave_py2` nanobind module from that build directory:

```console
cmake --build .
```

The default build stages the native module, its runtime dependencies, and its
assets directly in the ignored portions of `agave_pyvk/agave_pyvk`. From the
repository's `agave_pyvk` directory, install the package without another native
build:

```console
python -m pip install -e .
```

This editable-install hook uses the staged native module and never invokes
CMake, so it does not rebuild renderlib. Re-run `cmake --build .` after native
code changes; Python source edits are visible immediately. On Windows, run the
CMake build from a VS2026 x64 Native Tools Command Prompt.

To produce a wheel file from the same staged native build, install the `build`
frontend and run:

```console
python -m pip install build
python -m build --wheel .
```
