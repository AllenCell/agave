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

### How the build is organized

`agave_pyvk` produces a single Python-linked artifact: the nanobind extension
`_native.<abi>.so` (target `agave_py2`). Everything below it — `renderlib`,
libCZI, tensorstore, curl, and the AGAVE app — is plain C++ that does **not**
link `libpython` and therefore does **not** need to be rebuilt when you switch
Python interpreters. Only `agave_py2` is ABI-specific.

`pip` never builds the extension in this project. The `pyproject.toml` uses
plain `setuptools`; the `.so` is produced by CMake and staged into
`agave_pyvk/agave_pyvk/` by the `stage_agave_pyvk` target. `pip install -e .`
just registers the pure-Python package and picks up whatever `.so` CMake last
staged.

### First-time build

Configure the top-level AGAVE build against the Python environment you intend
to install into. The interpreter is baked into the CMake cache at configure
time — it is not re-detected by `cmake --build`.

```console
cmake -S <agave-root> -B <agave-root>/build -G Ninja \
      -DPython_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')" \
      -DCMAKE_BUILD_TYPE=Release
cmake --build <agave-root>/build --target stage_agave_pyvk
```

Then from `agave_pyvk/`:

```console
python -m pip install -e .
```

Python source edits are visible immediately; native edits require re-running
the `stage_agave_pyvk` target.

### Switching Python interpreters — minimal rebuild

The recommended workflow is: **activate the target Python environment before
you touch CMake.** `cmake --build .` alone will never re-detect the interpreter
— `Python_EXECUTABLE` is pinned in `CMakeCache.txt` at first configure — so
building from the wrong env silently produces a wrong-ABI extension.

If you want to retarget an existing `build` tree at a different Python without
rebuilding `renderlib` or the app, activate the new env and then run two
commands from the build directory:

```console
cd <agave-root>/build
cmake -DPython_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')" ..
cmake --build . --target stage_agave_pyvk
```

FindPython re-detects because `Python_EXECUTABLE` changed; nanobind's ABI
suffix cache is already cleared on every reconfigure by
`agave_pyvk/CMakeLists.txt`. Only `agave_py2` recompiles (a few seconds).
`stage_agave_pyvk` deletes any previously staged `_native*.so` / `_native*.pyd`
from the package directory and installs the fresh one, so the package never
ends up with two mismatched extensions.

If `import agave_pyvk` reports an ABI mismatch, the current interpreter and the
staged `.so` disagree. Re-run the two commands above from the environment you
want to use.

### Standalone build (skip the AGAVE app and tests)

```console
cmake -S <agave-root> -B <agave-root>/build -G Ninja \
      -DPython_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')" \
      -DAGAVE_BUILD_APP=OFF -DAGAVE_BUILD_TESTS=OFF -DAGAVE_BUILD_PYVK=ON \
      -DCMAKE_BUILD_TYPE=Release
cmake --build <agave-root>/build --target stage_agave_pyvk
```

### Building a wheel

Once the native module is staged for the desired interpreter:

```console
python -m pip install build
python -m build --wheel .
```
