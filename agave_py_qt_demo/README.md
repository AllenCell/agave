# agave_py Qt multi-canvas demo

A PyQt6 desktop app that shows a grid of remote OME-Zarr volumes. Each volume
is rendered by its own `agave_py.AgaveCanvas` (raymarch by default), and every
canvas shares a single Vulkan device. It mirrors
`zarr-webgpu-multicanvas-demo` and is meant for exercising multi-canvas device
sharing.

## Setup

Build and stage `agave_py` first (`cmake --build <build> --target stage_agave_py`).
Then install it and the demo's dependencies:

```powershell
pip install -e agave_py
pip install -r agave_py_qt_demo/requirements.txt
```

## Run

Run from this directory. From the repo root, the `agave_py/` project folder
shadows the installed package.

```powershell
cd agave_py_qt_demo
python zarr_grid.py --count 16
```

Volumes load in background threads at the multiresolution level one finer than
the coarsest. Each canvas is created on the GUI thread once its data arrives.
The **Level** selector lists every level of the first volume (with its XYZ
size) and reloads all tiles at that level; the new data goes into the existing
canvases with `load_array()`, so the shared Vulkan device stays up (the camera
resets, slice positions are kept proportionally). Drag a tile to orbit
it. With **Sync views** checked, all tiles orbit together. **Reset view**
reframes all tiles, and the **Density** slider controls volume density.
**Min %** / **Max %** set the intensity percentile window of every visible
channel using the existing `set_percentile_threshold()` API.

The **Renderer** selector switches every canvas between all of desktop AGAVE's
modes (Raymarch, Path trace, Z/Y/X slice, Triple slice) via
`AgaveCanvas.set_render_mode()`, without reloading data or losing the
camera. Path trace keeps refining each tile in the background up to
`--pathtrace-iterations` samples; any interaction restarts accumulation. In
Z/Y/X slice modes the **Slice** slider places the plane at that percentage of
each volume's extent, via `AgaveCanvas.set_slice_indices()`.

In Triple slice mode, drag inside a pane instead: grabbing near one crosshair
line moves just that plane, and grabbing anywhere else moves both of the
pane's planes to the pointer. With **Sync views** on, other tiles follow at the
same relative position. All of this is computed in Python
([triple_slice.py](triple_slice.py) mirrors renderlib's pane layout); the
canvas only receives `set_slice_indices(x, y, z)`.

| Flag | Default | Meaning |
| --- | --- | --- |
| `--count` | 16 | Number of zarrs (from `sources.py`) |
| `--level` | -2 | Initial multiresolution level; negative values count from the coarsest (-1) |
| `--time` | 0 | Time index |
| `--tile-size` | 256 | Canvas size in pixels |
| `--columns` | auto | Grid columns |
| `--gpu` | auto | Vulkan device index (all canvases share it) |
| `--density` | 50 | Initial density |
| `--pct-low` / `--pct-high` | 50 / 98.3 | Initial percentile window, in % |
| `--mode` | raymarch | Initial renderer: `raymarch`, `pathtrace`, `slice_z`, `slice_y`, `slice_x`, or `triple` |
| `--pathtrace-iterations` | 128 | Samples accumulated per tile in path trace mode |
| `--slice` | 50 | Initial slice position, as a percentage of each volume |
| `--max-channels` | 3 | Channels uploaded per volume |
| `--workers` | 8 | Loader threads |
| `--screenshot PATH` | — | Save a screenshot after loading, then exit |
| `--switch-mode MODE` | — | With `--screenshot`, switch every canvas to `MODE` before capturing |

The log shows one `Renderlib startup` for the whole grid and one
`Renderlib shutdown` when the window closes.
