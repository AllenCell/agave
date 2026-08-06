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

NumPy volumes can also be loaded directly. Three-dimensional arrays use ZYX
order and four-dimensional arrays use CZYX order:

```python
import numpy as np

volume = np.zeros((2, 64, 256, 256), dtype=np.uint16)
with AgaveRenderer(mode="pathtrace") as renderer:
    info = renderer.load_array(
        volume,
        name="example",
        voxel_size=(0.108, 0.108, 0.29),
        spatial_units="um",
        channel_names=("DNA", "membrane"),
    )
```

`uint8`, `uint16`, and `float32` arrays are accepted and copied into AGAVE's
channel-major `uint16` storage. `uint8` values are widened without rescaling;
each `float32` channel is independently min-max normalized to the full 16-bit
range. A new call replaces the current volume and represents one time point.

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
