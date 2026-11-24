"""Python-side hit testing for AGAVE's triple-slice view.

Mirrors ``computeTripleSliceLayout`` and the pane helpers in
``renderlib/SliceViewState.cpp`` so mouse drags can be turned into slice
indices without any native interaction API; the result is sent with
``set_slice_indices(x, y, z)``. Coordinates use a top-left origin, like Qt.
"""

import math
from dataclasses import dataclass

# Matches the pane gap used by renderlib's slice renderer and crosshair tool.
PANE_GAP = 2.0
# Pane -> (u axis, v axis), as indices into (x, y, z). V increases upward.
PANE_AXES = {"xy": (0, 1), "yz": (2, 1), "xz": (0, 2)}


@dataclass
class Rect:
    x: float
    y: float
    width: float
    height: float

    def contains(self, px: float, py: float) -> bool:
        return (
            self.width > 0
            and self.height > 0
            and self.x <= px <= self.x + self.width
            and self.y <= py <= self.y + self.height
        )


def layout(physical_xyz, width: int, height: int, gap: float = PANE_GAP):
    """Return {"xy", "yz", "xz"} pane rects, or {} if nothing fits."""
    x, y, z = (max(float(v), 1e-6) for v in physical_xyz)
    gap = max(0.0, gap)
    if width <= gap or height <= gap:
        return {}
    scale = min((width - gap) / (x + z), (height - gap) / (y + z))
    if not scale > 0:
        return {}
    xy_w = max(1.0, math.floor(x * scale))
    xy_h = max(1.0, math.floor(y * scale))
    z_px = max(1.0, math.floor(z * scale))
    total_w = xy_w + gap + z_px
    total_h = xy_h + gap + z_px
    offset_x = math.floor((width - total_w) * 0.5)
    offset_bottom = math.floor((height - total_h) * 0.5)
    offset_top = height - total_h - offset_bottom
    return {
        "xz": Rect(offset_x, offset_top, xy_w, z_px),
        "xy": Rect(offset_x, offset_top + z_px + gap, xy_w, xy_h),
        "yz": Rect(offset_x + xy_w + gap, offset_top + z_px + gap, z_px, xy_h),
    }


def pane_at(panes: dict, px: float, py: float) -> str | None:
    for name in ("xy", "yz", "xz"):
        if name in panes and panes[name].contains(px, py):
            return name
    return None


def _uv(rect: Rect, px: float, py: float) -> tuple[float, float]:
    u = min(1.0, max(0.0, (px - rect.x) / rect.width))
    v = min(1.0, max(0.0, 1.0 - (py - rect.y) / rect.height))
    return u, v


def _uv_to_index(uv: float, size: int) -> int:
    if size <= 1:
        return 0
    # floor(x + 0.5) matches std::lround for the non-negative values used here.
    return min(size - 1, max(0, math.floor(min(1.0, max(0.0, uv)) * (size - 1) + 0.5)))


def _index_to_uv(index: int, size: int) -> float:
    if size <= 1:
        return 0.5
    return min(size - 1, max(0, index)) / (size - 1)


def hit_axes(panes, pane, indices_xyz, dims_xyz, px, py, threshold=6.0):
    """Axes a drag starting at (px, py) in ``pane`` should move.

    Grabbing near one crosshair line moves only that axis; anywhere else in
    the pane moves both of its axes (jumping the crosshair to the pointer).
    """
    rect = panes[pane]
    u_axis, v_axis = PANE_AXES[pane]
    cx = rect.x + _index_to_uv(indices_xyz[u_axis], dims_xyz[u_axis]) * rect.width
    cy = rect.y + (1.0 - _index_to_uv(indices_xyz[v_axis], dims_xyz[v_axis])) * (
        rect.height
    )
    on_vertical = abs(px - cx) <= threshold
    on_horizontal = abs(py - cy) <= threshold
    if on_vertical and not on_horizontal:
        return (u_axis,)
    if on_horizontal and not on_vertical:
        return (v_axis,)
    return (u_axis, v_axis)


def drag_indices(panes, pane, axes, indices_xyz, dims_xyz, px, py):
    """New (x, y, z) indices after dragging ``axes`` of ``pane`` to (px, py)."""
    u, v = _uv(panes[pane], px, py)
    u_axis, v_axis = PANE_AXES[pane]
    result = list(indices_xyz)
    if u_axis in axes:
        result[u_axis] = _uv_to_index(u, dims_xyz[u_axis])
    if v_axis in axes:
        result[v_axis] = _uv_to_index(v, dims_xyz[v_axis])
    return tuple(result)
