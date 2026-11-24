"""PyQt6 grid of AGAVE-rendered OME-Zarr volumes in any AGAVE render mode.

Every tile is an ``agave_py.AgaveCanvas`` with its own scene, camera, renderer,
and framebuffer. All canvases share one process-wide Vulkan device, which is
created by the first canvas and destroyed when the last one closes.
"""

import argparse
import math
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass

import numpy
import zarr
from agave_py import AgaveCanvas
from PyQt6.QtCore import QObject, QPointF, Qt, QTimer, pyqtSignal
from PyQt6.QtGui import QImage, QMouseEvent, QPixmap
from PyQt6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QPushButton,
    QScrollArea,
    QSlider,
    QVBoxLayout,
    QWidget,
)

import camera
import triple_slice
from sources import ZARR_SOURCES

# Channel colors in display order; brightfield-like channels are hidden.
PALETTE = [
    (1.0, 0.0, 1.0),
    (0.0, 1.0, 0.0),
    (0.0, 1.0, 1.0),
    (1.0, 1.0, 0.0),
    (1.0, 0.5, 0.0),
    (0.3, 0.5, 1.0),
]
# Vol-E's default percentile window (in %), also used by the WebGPU demo.
PCT_LOW = 50.0
PCT_HIGH = 98.3
# (selector label, agave_py mode), matching desktop AGAVE's renderer list.
MODES = [
    ("Raymarch", "raymarch"),
    ("Path trace", "pathtrace"),
    ("Z slice", "slice_z"),
    ("Y slice", "slice_y"),
    ("X slice", "slice_x"),
    ("Triple slice", "triple"),
]
SLICE_MODES = ("slice_z", "slice_y", "slice_x", "triple")
# Volume loader: True reads through bioio + bioio-ome-zarr, False through zarr
# directly. Flip to compare load times (status bar, stdout, and tile captions).
USE_BIOIO = True


@dataclass
class LoadedVolume:
    url: str
    label: str
    data: numpy.ndarray  # CZYX
    voxel_size: tuple
    spatial_units: str
    channel_names: list[str]
    level_path: str
    level_count: int
    level_index: int
    level_shapes: list[tuple]  # ZYX shape of every level, full resolution first
    time_index: int
    time_count: int
    load_seconds: float


@dataclass
class LoadFailure:
    url: str
    label: str
    error: str


def source_label(url: str) -> str:
    name = url.rstrip("/").rsplit("/", 1)[-1]
    return name.removesuffix(".ome.zarr").removesuffix(".zarr")


def resolve_level(level: int, count: int) -> int:
    """Non-negative levels index from full resolution; negative from coarsest."""
    index = level if level >= 0 else count + level
    return max(0, min(count - 1, index))


def load_volume(url: str, level: int, time_index: int) -> LoadedVolume:
    if USE_BIOIO:
        return load_volume_bioio(url, level, time_index)
    return load_volume_zarr(url, level, time_index)


def load_volume_bioio(url: str, level: int, time_index: int) -> LoadedVolume:
    # Imported lazily so the zarr path works without bioio installed.
    import bioio_ome_zarr
    from bioio import BioImage

    start = time.perf_counter()
    image = BioImage(url, reader=bioio_ome_zarr.Reader)
    level_dims = image.resolution_level_dims
    levels = sorted(level_dims)
    level_index = resolve_level(level, len(levels))
    image.set_resolution_level(levels[level_index])
    # bioio always reports TCZYX shapes.
    level_shapes = [tuple(level_dims[lvl][-3:]) for lvl in levels]

    t = max(0, min(time_index, image.dims.T - 1))
    data = numpy.asarray(image.get_image_data("CZYX", T=t))

    sizes = image.physical_pixel_sizes
    voxel_size = tuple(float(s) if s else 1.0 for s in (sizes.X, sizes.Y, sizes.Z))
    unit = image.dimension_properties.X.unit

    names = list(image.channel_names or [])
    if len(names) != data.shape[0]:
        names = [f"Ch{i}" for i in range(data.shape[0])]

    if data.dtype not in (numpy.uint8, numpy.uint16, numpy.float32):
        data = data.astype(numpy.float32)

    return LoadedVolume(
        url=url,
        label=source_label(url),
        data=numpy.ascontiguousarray(data),
        voxel_size=voxel_size,
        spatial_units=str(unit) if unit else "units",
        channel_names=names,
        level_path=str(levels[level_index]),
        level_count=len(levels),
        level_index=level_index,
        level_shapes=level_shapes,
        time_index=t,
        time_count=image.dims.T,
        load_seconds=time.perf_counter() - start,
    )


def load_volume_zarr(url: str, level: int, time_index: int) -> LoadedVolume:
    start = time.perf_counter()
    group = zarr.open_group(url, mode="r")
    multiscale = group.attrs["multiscales"][0]
    datasets = multiscale["datasets"]
    level_index = resolve_level(level, len(datasets))
    dataset = datasets[level_index]
    axes = [axis["name"].lower() for axis in multiscale.get("axes", [])]
    if not axes:
        axes = list("tczyx")[-group[dataset["path"]].ndim :]
    # Array metadata only; used to label the level selector.
    level_shapes = []
    for ds in datasets:
        shape = group[ds["path"]].shape
        level_shapes.append(
            tuple(shape[axes.index(n)] if n in axes else 1 for n in "zyx")
        )

    array = group[dataset["path"]]
    time_count = array.shape[axes.index("t")] if "t" in axes else 1
    t = max(0, min(time_index, time_count - 1))
    selection = tuple(t if name == "t" else slice(None) for name in axes)
    data = numpy.asarray(array[selection])
    kept = [name for name in axes if name != "t"]
    if "c" not in kept:
        data = data[numpy.newaxis]
        kept = ["c", *kept]
    data = numpy.transpose(data, [kept.index(name) for name in "czyx"])

    scale = [1.0] * len(axes)
    for transform in dataset.get("coordinateTransformations", []):
        if transform.get("type") == "scale":
            scale = transform["scale"]
    voxel_size = tuple(float(scale[axes.index(name)]) for name in "xyz")
    unit = next(
        (a.get("unit") for a in multiscale.get("axes", []) if a["name"] == "x"),
        None,
    )

    channels = group.attrs.get("omero", {}).get("channels", [])
    names = [ch.get("label", f"Ch{i}") for i, ch in enumerate(channels)]
    if len(names) != data.shape[0]:
        names = [f"Ch{i}" for i in range(data.shape[0])]

    if data.dtype not in (numpy.uint8, numpy.uint16, numpy.float32):
        data = data.astype(numpy.float32)

    return LoadedVolume(
        url=url,
        label=source_label(url),
        data=numpy.ascontiguousarray(data),
        voxel_size=voxel_size,
        spatial_units=unit or "units",
        channel_names=names,
        level_path=str(dataset["path"]),
        level_count=len(datasets),
        level_index=level_index,
        level_shapes=level_shapes,
        time_index=t,
        time_count=time_count,
        load_seconds=time.perf_counter() - start,
    )


class LoadSignals(QObject):
    # Emitted from worker threads; Qt queues delivery onto the GUI thread.
    finished = pyqtSignal(int, int, object)  # generation, tile index, result


class Tile(QWidget):
    """One grid cell: a label showing an AgaveCanvas frame plus a caption."""

    orbit_requested = pyqtSignal(object, float, float)
    # (tile, new xyz indices, axes the drag moves)
    slice_dragged = pyqtSignal(object, tuple, tuple)

    def __init__(self, label: str, size: int) -> None:
        super().__init__()
        self.canvas: AgaveCanvas | None = None
        self.shape_zyx = (1, 1, 1)
        self.voxel_size = (1.0, 1.0, 1.0)
        # The demo owns the camera; the canvas only receives eye/target/up.
        self.orientation = camera.FRONT
        self.slice_xyz = (0, 0, 0)
        self.visible_channels: list[int] = []
        # In triple-slice mode a drag moves the slice planes instead of orbiting.
        self.triple = False
        self.size = size
        self.dirty = False
        # Path tracing accumulates one sample per render; raymarch needs only one.
        self.iterations = 0
        self.last_render_ms = 0.0
        self._drag_from: QPointF | None = None
        self._slice_drag: tuple | None = None  # (panes, pane, axes)

        self.image = QLabel("loading…")
        self.image.setFixedSize(size, size)
        self.image.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.image.setStyleSheet("background: black; color: #888;")
        self.caption = QLabel(label)
        self.caption.setFixedWidth(size)
        self.caption.setStyleSheet("color: #ccc; font-size: 10px;")

        layout = QVBoxLayout(self)
        layout.setContentsMargins(2, 2, 2, 2)
        layout.setSpacing(1)
        layout.addWidget(self.image)
        layout.addWidget(self.caption)

    def attach(
        self, canvas: AgaveCanvas, shape_zyx: tuple, voxel_size: tuple, caption: str
    ) -> None:
        self.canvas = canvas
        self.shape_zyx = shape_zyx
        self.voxel_size = voxel_size
        self.caption.setText(caption)
        # Loading resets the native camera, so always resend ours.
        self.apply_camera()

    def apply_camera(self) -> None:
        physical = tuple(n * s for n, s in zip(self.dims_xyz, self.voxel_size))
        eye, target, up = camera.pose(physical, self.orientation)
        self.canvas.eye(*eye)
        self.canvas.target(*target)
        self.canvas.up(*up)
        self.invalidate()

    @property
    def dims_xyz(self) -> tuple:
        z, y, x = self.shape_zyx
        return (x, y, z)

    def set_slice(self, xyz: tuple) -> None:
        # The demo owns the slice state; the canvas only receives the indices.
        self.slice_xyz = tuple(
            min(max(0, int(i)), n - 1) for i, n in zip(xyz, self.dims_xyz)
        )
        self.canvas.set_slice_indices(*self.slice_xyz)
        self.invalidate()

    def triple_panes(self) -> dict:
        physical = tuple(n * s for n, s in zip(self.dims_xyz, self.voxel_size))
        return triple_slice.layout(physical, self.size, self.size)

    def invalidate(self) -> None:
        self.dirty = True
        self.iterations = 0

    def needs_render(self, target_iterations: int) -> bool:
        return self.canvas is not None and (
            self.dirty or self.iterations < target_iterations
        )

    def fail(self, message: str) -> None:
        self.image.setText("load failed")
        self.image.setToolTip(message)

    def render(self) -> None:
        if self.canvas is None:
            return
        self.dirty = False
        self.iterations += 1
        start = time.perf_counter()
        frame = self.canvas.render_frame()
        self.last_render_ms = (time.perf_counter() - start) * 1000.0
        height, width, _ = frame.shape
        # BGRA bytes on a little-endian host are QImage's 0xAARRGGBB layout.
        # copy() detaches from the Python buffer, which QPixmap may otherwise share.
        pixels = frame.tobytes()
        image = QImage(
            pixels, width, height, width * 4, QImage.Format.Format_RGB32
        ).copy()
        self.image.setPixmap(QPixmap.fromImage(image))

    def _image_pos(self, event: QMouseEvent) -> QPointF:
        # The canvas renders at the label's size, so label pixels are canvas pixels.
        return event.position() - QPointF(self.image.pos())

    def mousePressEvent(self, event: QMouseEvent) -> None:
        if event.button() != Qt.MouseButton.LeftButton or self.canvas is None:
            return
        if self.triple:
            pos = self._image_pos(event)
            panes = self.triple_panes()
            pane = triple_slice.pane_at(panes, pos.x(), pos.y())
            if pane is not None:
                axes = triple_slice.hit_axes(
                    panes, pane, self.slice_xyz, self.dims_xyz, pos.x(), pos.y()
                )
                self._slice_drag = (panes, pane, axes)
                self._drag_slice_to(pos)
            return
        self._drag_from = event.position()

    def _drag_slice_to(self, pos: QPointF) -> None:
        panes, pane, axes = self._slice_drag
        xyz = triple_slice.drag_indices(
            panes, pane, axes, self.slice_xyz, self.dims_xyz, pos.x(), pos.y()
        )
        if xyz != self.slice_xyz:
            self.slice_dragged.emit(self, xyz, axes)

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if self._slice_drag is not None:
            self._drag_slice_to(self._image_pos(event))
            return
        if self._drag_from is None or self.canvas is None:
            return
        delta = event.position() - self._drag_from
        self._drag_from = event.position()
        self.orbit_requested.emit(self, -delta.y() * 0.5, -delta.x() * 0.5)

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        self._drag_from = None
        self._slice_drag = None


class GridWindow(QMainWindow):
    def __init__(self, args: argparse.Namespace) -> None:
        super().__init__()
        self.args = args
        self.urls = ZARR_SOURCES[: args.count]
        self.tiles: list[Tile] = []
        self.pending = 0
        # Bumped on every level change so stale downloads are ignored.
        self.generation = 0
        self.levels_populated = False
        self.level = args.level
        self.time_index = max(0, args.time)
        self.futures = []
        self.load_started = time.perf_counter()
        self.load_seconds = 0.0
        self.mode = args.mode

        self.renderer = QComboBox()
        for label, mode in MODES:
            self.renderer.addItem(label, mode)
        self.renderer.setCurrentIndex(self.renderer.findData(self.mode))
        self.renderer.currentIndexChanged.connect(
            lambda _: self.set_mode(self.renderer.currentData())
        )
        # Filled with the real levels once the first volume's metadata arrives.
        self.level_box = QComboBox()
        self.level_box.setSizeAdjustPolicy(QComboBox.SizeAdjustPolicy.AdjustToContents)
        self.level_box.addItem(f"level {args.level}", args.level)
        self.level_box.currentIndexChanged.connect(
            lambda _: self.set_level(self.level_box.currentData())
        )
        self.sync = QCheckBox("Sync views")
        self.sync.setChecked(True)
        reset = QPushButton("Reset view")
        reset.clicked.connect(self.reset_views)
        self.density = QSlider(Qt.Orientation.Horizontal)
        self.density.setRange(1, 200)
        self.density.setValue(args.density)
        self.density.setFixedWidth(120)
        self.density.valueChanged.connect(self.apply_density)
        # Percentile window applied to every visible channel.
        self.pct_low = self.percent_box(args.pct_low)
        self.pct_high = self.percent_box(args.pct_high)
        self.pct_low.valueChanged.connect(self.apply_percentiles)
        self.pct_high.valueChanged.connect(self.apply_percentiles)
        # Slice position as a fraction of each volume's extent, for every axis.
        # Triple-slice mode is driven by dragging in the panes instead.
        self.slice = QSlider(Qt.Orientation.Horizontal)
        self.slice.setRange(0, 100)
        self.slice.setValue(max(0, min(100, args.slice)))
        self.slice.setFixedWidth(120)
        self.slice.valueChanged.connect(self.apply_slice)
        # Time point; the range is filled in from the first volume's metadata.
        # Tracking is off so dragging reloads once on release, not per tick.
        self.time_slider = QSlider(Qt.Orientation.Horizontal)
        self.time_slider.setRange(0, self.time_index)
        self.time_slider.setValue(self.time_index)
        self.time_slider.setTracking(False)
        self.time_slider.setMinimumWidth(240)
        self.time_label = QLabel()
        self.time_slider.sliderMoved.connect(self.show_time)
        self.time_slider.valueChanged.connect(self.set_time)
        self.show_time(self.time_index)
        self.status = QLabel()

        top = QHBoxLayout()
        top.addWidget(QLabel("Renderer"))
        top.addWidget(self.renderer)
        top.addWidget(QLabel("Level"))
        top.addWidget(self.level_box)
        top.addWidget(QLabel("Time"))
        top.addWidget(self.time_slider, 1)
        top.addWidget(self.time_label)

        toolbar = QHBoxLayout()
        toolbar.addWidget(self.sync)
        toolbar.addWidget(reset)
        toolbar.addWidget(QLabel("Density"))
        toolbar.addWidget(self.density)
        toolbar.addWidget(QLabel("Min %"))
        toolbar.addWidget(self.pct_low)
        toolbar.addWidget(QLabel("Max %"))
        toolbar.addWidget(self.pct_high)
        toolbar.addWidget(QLabel("Slice"))
        toolbar.addWidget(self.slice)
        toolbar.addStretch(1)

        grid_widget = QWidget()
        grid = QGridLayout(grid_widget)
        grid.setSpacing(4)
        columns = args.columns or max(1, math.ceil(math.sqrt(len(self.urls))))
        for index, url in enumerate(self.urls):
            tile = Tile(source_label(url), args.tile_size)
            tile.orbit_requested.connect(self.orbit)
            tile.slice_dragged.connect(self.drag_slice)
            grid.addWidget(tile, index // columns, index % columns)
            self.tiles.append(tile)

        scroll = QScrollArea()
        scroll.setWidget(grid_widget)
        scroll.setWidgetResizable(True)
        scroll.setStyleSheet("background: #202020;")

        central = QWidget()
        layout = QVBoxLayout(central)
        layout.addLayout(top)
        layout.addLayout(toolbar)
        layout.addWidget(self.status)
        layout.addWidget(scroll)
        self.setCentralWidget(central)
        self.resize(
            min(1800, max(1100, columns * (args.tile_size + 8) + 40)),
            min(
                1100, math.ceil(len(self.urls) / columns) * (args.tile_size + 26) + 130
            ),
        )

        # Rendering is coalesced: input marks tiles dirty, one timer tick renders them.
        self.render_timer = QTimer(self)
        self.render_timer.setSingleShot(True)
        self.render_timer.timeout.connect(self.render_dirty)

        self.signals = LoadSignals()
        self.signals.finished.connect(self.on_loaded)
        self.executor = ThreadPoolExecutor(max_workers=args.workers)
        self.update_mode_controls()
        self.start_loading()
        self.update_title()

    @staticmethod
    def percent_box(value: float) -> QDoubleSpinBox:
        box = QDoubleSpinBox()
        box.setRange(0.0, 100.0)
        box.setDecimals(1)
        box.setSingleStep(0.5)
        box.setSuffix(" %")
        box.setValue(value)
        # Apply on enter/arrows/focus loss rather than on every keystroke.
        box.setKeyboardTracking(False)
        return box

    def start_loading(self) -> None:
        self.generation += 1
        # Drop downloads that haven't started; their results would be stale.
        for future in self.futures:
            future.cancel()
        self.pending = len(self.urls)
        self.load_started = time.perf_counter()
        self.futures = [
            self.executor.submit(
                self._load, self.generation, index, url, self.level, self.time_index
            )
            for index, url in enumerate(self.urls)
        ]
        self.update_status()

    def set_level(self, level: int) -> None:
        if level is None or level == self.level:
            return
        self.level = level
        # Canvases stay alive; on_loaded swaps the new data into each one.
        self.start_loading()

    def show_time(self, value: int) -> None:
        self.time_label.setText(f"{value} / {self.time_slider.maximum()}")

    def set_time(self, value: int) -> None:
        self.show_time(value)
        if value == self.time_index:
            return
        self.time_index = value
        self.start_loading()

    def populate_time(self, result: "LoadedVolume") -> None:
        self.time_slider.blockSignals(True)
        self.time_slider.setRange(0, max(0, result.time_count - 1))
        self.time_slider.setValue(result.time_index)
        self.time_slider.blockSignals(False)
        self.time_index = result.time_index
        self.show_time(result.time_index)

    def populate_levels(self, result: "LoadedVolume") -> None:
        # Items are relative to the coarsest level so they apply to every zarr.
        count = result.level_count
        self.level_box.blockSignals(True)
        self.level_box.clear()
        for index, (z, y, x) in enumerate(result.level_shapes):
            note = " (coarsest)" if index == count - 1 else ""
            note = " (full)" if index == 0 and count > 1 else note
            self.level_box.addItem(f"{index}: {x}x{y}x{z}{note}", index - count)
        self.level_box.setCurrentIndex(result.level_index)
        self.level_box.setMinimumWidth(self.level_box.sizeHint().width())
        self.level_box.blockSignals(False)
        self.level = result.level_index - count
        self.levels_populated = True

    def update_mode_controls(self) -> None:
        triple = self.mode == "triple"
        self.slice.setEnabled(self.mode in SLICE_MODES and not triple)
        for tile in self.tiles:
            tile.triple = triple

    def target_iterations(self) -> int:
        return self.args.pathtrace_iterations if self.mode == "pathtrace" else 1

    def update_title(self) -> None:
        self.setWindowTitle(f"AGAVE Python canvases — OME-Zarr grid ({self.mode})")

    def set_mode(self, mode: str) -> None:
        if mode == self.mode:
            return
        self.mode = mode
        # Each canvas swaps its renderer in place; data, camera and colors are kept.
        for tile in self.live_tiles():
            tile.canvas.set_render_mode(mode)
            tile.invalidate()
        self.update_mode_controls()
        self.update_title()
        self.schedule_render()

    def _load(
        self, generation: int, index: int, url: str, level: int, time_index: int
    ) -> None:
        try:
            result = load_volume(url, level, time_index)
        except (
            Exception
        ) as error:  # noqa: BLE001 - surface any loader failure in the tile
            result = LoadFailure(
                url, source_label(url), f"{type(error).__name__}: {error}"
            )
        self.signals.finished.emit(generation, index, result)

    def on_loaded(self, generation: int, index: int, result) -> None:
        if generation != self.generation:
            return
        self.pending -= 1
        if self.pending == 0:
            self.load_seconds = time.perf_counter() - self.load_started
            print(
                f"Loaded {len(self.tiles)} volumes via "
                f"{'bioio' if USE_BIOIO else 'zarr'} in {self.load_seconds:.2f}s"
            )
        tile = self.tiles[index]
        if isinstance(result, LoadFailure):
            print(f"[{index}] {result.label}: {result.error}", file=sys.stderr)
            tile.fail(result.error)
        else:
            if not self.levels_populated:
                self.populate_levels(result)
                self.populate_time(result)
            # Keep each slice at the same relative position across levels.
            old_dims = tile.dims_xyz
            fractions = [
                i / (n - 1) if n > 1 else 0.5 for i, n in zip(tile.slice_xyz, old_dims)
            ]
            reload = tile.canvas is not None
            # Canvas creation and every render call stay on the GUI thread.
            if reload:
                tile.canvas.load_array(
                    result.data,
                    name=result.label,
                    voxel_size=result.voxel_size,
                    spatial_units=result.spatial_units,
                    channel_names=result.channel_names,
                )
            else:
                # A late-arriving tile joins the synced view.
                live = self.live_tiles()
                if self.sync.isChecked() and live:
                    tile.orientation = live[0].orientation
                tile.canvas = AgaveCanvas(
                    result.data,
                    mode=self.mode,
                    gpu=self.args.gpu,
                    name=result.label,
                    voxel_size=result.voxel_size,
                    spatial_units=result.spatial_units,
                    channel_names=result.channel_names,
                    width=self.args.tile_size,
                    height=self.args.tile_size,
                )
            self.configure_canvas(tile.canvas, result.channel_names)
            tile.visible_channels = self.visible_channels(result.channel_names)
            c, z, y, x = result.data.shape
            tile.attach(
                tile.canvas,
                (z, y, x),
                result.voxel_size,
                f"{result.label}  L{result.level_path}/{result.level_count - 1}  "
                f"t{result.time_index}  "
                f"{c}x{z}x{y}x{x}  {result.load_seconds:.1f}s",
            )
            if reload:
                tile.set_slice(
                    tuple(round(f * (n - 1)) for f, n in zip(fractions, tile.dims_xyz))
                )
            else:
                self.set_tile_slice(tile)
            self.schedule_render()
        self.update_status()
        if self.pending == 0 and self.args.screenshot:
            QTimer.singleShot(250, self.save_screenshot_and_quit)

    def visible_channels(self, channel_names: list[str]) -> list[int]:
        return [
            channel
            for channel, name in enumerate(channel_names)
            if "bright" not in name.lower() and channel < self.args.max_channels
        ]

    def configure_canvas(self, canvas: AgaveCanvas, channel_names: list[str]) -> None:
        canvas.background_color(0.0, 0.0, 0.0)
        canvas.density(float(self.density.value()))
        visible = set(self.visible_channels(channel_names))
        low, high = self.percentiles()
        color_index = 0
        for channel in range(len(channel_names)):
            canvas.enable_channel(channel, int(channel in visible))
            if channel not in visible:
                continue
            r, g, b = PALETTE[color_index % len(PALETTE)]
            color_index += 1
            canvas.mat_diffuse(channel, r, g, b, 1.0)
            canvas.set_percentile_threshold(channel, low, high)

    def percentiles(self) -> tuple[float, float]:
        low = self.pct_low.value() / 100.0
        high = self.pct_high.value() / 100.0
        return min(low, high), max(low, high)

    def apply_percentiles(self, _value: float) -> None:
        # The existing set_percentile_threshold API recomputes each channel's
        # intensity window from its histogram.
        low, high = self.percentiles()
        for tile in self.live_tiles():
            for channel in tile.visible_channels:
                tile.canvas.set_percentile_threshold(channel, low, high)
            tile.invalidate()
        self.schedule_render()

    def drag_slice(self, source: Tile, xyz: tuple, axes: tuple) -> None:
        source.set_slice(xyz)
        if self.sync.isChecked():
            # Other volumes follow at the same relative position on the dragged axes.
            dims = source.dims_xyz
            for tile in self.live_tiles():
                if tile is source:
                    continue
                moved = list(tile.slice_xyz)
                for axis in axes:
                    fraction = xyz[axis] / (dims[axis] - 1) if dims[axis] > 1 else 0.5
                    moved[axis] = round(fraction * (tile.dims_xyz[axis] - 1))
                tile.set_slice(tuple(moved))
        self.schedule_render()

    def live_tiles(self) -> list[Tile]:
        return [tile for tile in self.tiles if tile.canvas is not None]

    def orbit(self, source: Tile, theta: float, phi: float) -> None:
        targets = self.live_tiles() if self.sync.isChecked() else [source]
        for tile in targets:
            tile.orientation = camera.orbit(tile.orientation, theta, phi)
            tile.apply_camera()
        self.schedule_render()

    def reset_views(self) -> None:
        for tile in self.live_tiles():
            tile.orientation = camera.FRONT
            tile.apply_camera()
        self.schedule_render()

    def apply_density(self, value: int) -> None:
        for tile in self.live_tiles():
            tile.canvas.density(float(value))
            tile.invalidate()
        self.schedule_render()

    def set_tile_slice(self, tile: Tile) -> None:
        fraction = self.slice.value() / 100.0
        tile.set_slice(tuple(round(fraction * (n - 1)) for n in tile.dims_xyz))

    def apply_slice(self, _value: int) -> None:
        for tile in self.live_tiles():
            self.set_tile_slice(tile)
        self.schedule_render()

    def schedule_render(self) -> None:
        if not self.render_timer.isActive():
            self.render_timer.start(0)

    def render_dirty(self) -> None:
        """Render one pass over tiles that are dirty or still converging."""
        start = time.perf_counter()
        target = self.target_iterations()
        rendered = 0
        for tile in self.tiles:
            if tile.needs_render(target):
                tile.render()
                rendered += 1
        if rendered:
            self.last_frame = (rendered, (time.perf_counter() - start) * 1000.0)
        self.update_status()
        # Keep refining path-traced tiles; input events still run between passes.
        if any(tile.needs_render(target) for tile in self.tiles):
            self.schedule_render()

    def update_status(self) -> None:
        live = self.live_tiles()
        text = f"{len(live)}/{len(self.tiles)} canvases on one shared Vulkan device"
        if self.pending:
            text += f" — loading {self.pending}…"
        else:
            loader = "bioio" if USE_BIOIO else "zarr"
            text += f" — loaded via {loader} in {self.load_seconds:.1f}s"
        if self.mode == "pathtrace" and live:
            done = min(tile.iterations for tile in live)
            text += f" — iteration {done}/{self.args.pathtrace_iterations}"
        frame = getattr(self, "last_frame", None)
        if frame:
            text += f" — last pass: {frame[0]} tiles in {frame[1]:.0f} ms"
        self.status.setText(text)

    def save_screenshot_and_quit(self) -> None:
        if self.args.switch_mode:
            self.renderer.setCurrentIndex(self.renderer.findData(self.args.switch_mode))
        self.render_timer.stop()
        target = self.target_iterations()
        while any(tile.needs_render(target) for tile in self.tiles):
            self.render_dirty()
            self.render_timer.stop()
        self.grab().save(self.args.screenshot)
        print(f"Saved {self.args.screenshot}: {self.status.text()}")
        self.close()

    def closeEvent(self, event) -> None:
        self.executor.shutdown(wait=False, cancel_futures=True)
        # Closing the last canvas releases the shared Vulkan device.
        for tile in self.tiles:
            if tile.canvas is not None:
                tile.canvas.close()
                tile.canvas = None
        super().closeEvent(event)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--count",
        type=int,
        default=16,
        help=f"number of zarrs to load (max {len(ZARR_SOURCES)})",
    )
    parser.add_argument(
        "--level",
        type=int,
        default=-2,
        help="multiscale level; >= 0 counts from full resolution, negative from "
        "coarsest (default -2 = one finer than coarsest); change it with Level",
    )
    parser.add_argument("--time", type=int, default=0, help="time index to load")
    parser.add_argument(
        "--tile-size",
        type=int,
        default=256,
        help="render size in pixels for each canvas",
    )
    parser.add_argument(
        "--columns", type=int, default=0, help="grid columns (default: square-ish)"
    )
    parser.add_argument(
        "--gpu",
        type=int,
        default=-1,
        help="Vulkan device index shared by every canvas (-1 = auto)",
    )
    parser.add_argument(
        "--density", type=int, default=50, help="initial volume density"
    )
    parser.add_argument(
        "--pct-low", type=float, default=PCT_LOW, help="initial min percentile (%%)"
    )
    parser.add_argument(
        "--pct-high", type=float, default=PCT_HIGH, help="initial max percentile (%%)"
    )
    parser.add_argument(
        "--mode",
        choices=[mode for _, mode in MODES],
        default="raymarch",
        help="initial renderer; switch at runtime with the Renderer selector",
    )
    parser.add_argument(
        "--pathtrace-iterations",
        type=int,
        default=128,
        help="samples to accumulate per tile before path tracing stops refining",
    )
    parser.add_argument(
        "--max-channels", type=int, default=3, help="display at most this many channels"
    )
    parser.add_argument(
        "--workers", type=int, default=8, help="parallel zarr download threads"
    )
    parser.add_argument(
        "--screenshot",
        default="",
        help="save the window to this PNG after loading, then exit",
    )
    parser.add_argument(
        "--switch-mode",
        choices=[mode for _, mode in MODES],
        default="",
        help="with --screenshot: switch every canvas to this mode before capturing",
    )
    parser.add_argument(
        "--slice",
        type=int,
        default=50,
        help="initial slice position as a percentage of each volume (slice modes)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if USE_BIOIO:
        # bioio pulls in dask and xarray (~1.3 s). Import once up front so the
        # cost isn't counted as load time and loader threads don't serialize
        # on Python's import lock.
        import bioio  # noqa: F401
        import bioio_ome_zarr  # noqa: F401
    app = QApplication(sys.argv[:1])
    window = GridWindow(args)
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
