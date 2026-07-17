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
