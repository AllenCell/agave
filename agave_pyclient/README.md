# agave_pyclient

A Python client for the AGAVE 3d volume renderer

---

## Features

- Connects to AGAVE server and sends draw commands. Receives and saves rendered images.

## Quick Start

You must have AGAVE installed. On command line, run:

```
agave --server &
```

For Linux headless operation, you need to tell the Qt library to use the offscreen platform plugin:

```
agave -platform offscreen --server &
```

```python
from agave_pyclient import AgaveRenderer

# 1. connect to the AGAVE server
r = agave_pyclient.AgaveRenderer()
# 2. tell it what data to load
r.load_data("my_favorite.ome.tiff")
# 3. set some render settings (abbreviated list here)
r.set_resolution(681, 612)
r.background_color(0, 0, 0)
r.render_iterations(128)
r.set_primary_ray_step_size(4)
r.set_secondary_ray_step_size(4)
r.set_voxel_scale(0.270833, 0.270833, 0.53)
r.exposure(0.75)
r.density(28.7678)
# 4. give the output a name
r.session("output.png")
# 5. wait for render and then save output
r.redraw()
```

## Installation

**Stable Release:** `pip install agave_pyclient`<br>

## Documentation

For full package documentation please visit [allencell.github.io/agave](https://allencell.github.io/agave).

## Development

See [CONTRIBUTING.md](CONTRIBUTING.md) for information related to developing the code.

## The Four Commands You Need To Know

1. `pip install -e .[dev]`

   This will install your package in editable mode with all the required development
   dependencies (i.e. `tox`).

2. `make build`

   This will run `tox` which will run all your tests in both Python 3.7
   and Python 3.8 as well as linting your code.

3. `make clean`

   This will clean up various Python and build generated files so that you can ensure
   that you are working in a clean environment.

4. `make docs`

   This will generate and launch a web browser to view the most up-to-date
   documentation for your Python package.

**Allen Institute Software License**
