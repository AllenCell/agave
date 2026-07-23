# agave_pyvk

`agave_pyvk` is the standalone, headless, in-process Vulkan Python package for
AGAVE. It uses nanobind to execute Python commands directly in renderlib. It has no Qt or
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

Configure the normal top-level AGAVE build using the Python environment where
the package will be installed. Then build renderlib, the AGAVE application, and
the ABI-specific native module from that build directory:

```console
cmake --build .
```

From the repository's `agave_pyvk` directory, install the package into that
same Python environment:

```console
python -m pip install -e .
```

The CMake build stages `_native` and its runtime dependencies into the package
directory. Pip performs only normal Python packaging and does not invoke CMake.
Python source edits are visible immediately.

From this point you can import agave_pyvk and its AgaveRenderer class.

To produce a wheel file from the same staged native build, install the `build`
frontend and run:

```console
python -m pip install build
python -m build --wheel .
```
