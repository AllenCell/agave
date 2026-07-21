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

Configure the normal top-level AGAVE build once. Then build renderlib and the
AGAVE application from that build directory:

```console
cmake --build .
```

The top-level configure records its build directory in the ignored
`agave_pyvk/.cmake-build-dir` marker. From the repository's `agave_pyvk`
directory, install the package for the active Python interpreter:

```console
python -m pip install -e .
```

The pip backend reads the marker, reconfigures Python discovery for the active
interpreter, and builds only the `stage_agave_pyvk` target. That target links
against the existing renderlib CMake target, so renderlib is reused unless it is
out of date. Python source edits are visible immediately. On Windows, run pip
from a VS2026 x64 Native Tools Command Prompt so the compiler environment is
available.

To produce a wheel file from the same staged native build, install the `build`
frontend and run:

```console
python -m pip install build
python -m build --wheel .
```
