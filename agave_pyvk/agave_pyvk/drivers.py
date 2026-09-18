"""Drivers and the segment helpers that build them.

A driver produces values for one or more channels over a frame span. Derived
math belongs here, upstream of the channels, which keeps every channel a 1:1
mapping onto a single AGAVE call.

The vector helpers below are deliberately local rather than reused from
``agave.py``: those are numpy-based, and ``get_vertical_axis`` there returns the
*negated* up vector (``get_vertical_axis([1,0,0], [0,0,1])`` gives
``[0,0,-1]``), which is not the convention this module needs.
"""

import math
from typing import Dict, Sequence, Tuple

from .animation import (
    Driver,
    Ease,
    Keyframes,
    Segment,
    Value,
    as_value,
    lerp,
    linear,
)

Vec3 = Tuple[float, float, float]


# --- vector math ------------------------------------------------------------


def sub(a: Sequence[float], b: Sequence[float]) -> Tuple[float, ...]:
    return tuple(x - y for x, y in zip(a, b))


def add(a: Sequence[float], b: Sequence[float]) -> Tuple[float, ...]:
    return tuple(x + y for x, y in zip(a, b))


def scale(a: Sequence[float], k: float) -> Tuple[float, ...]:
    return tuple(x * k for x in a)


def dot(a: Sequence[float], b: Sequence[float]) -> float:
    return sum(x * y for x, y in zip(a, b))


def cross(a: Sequence[float], b: Sequence[float]) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def normalize(a: Sequence[float]) -> Tuple[float, ...]:
    length = math.sqrt(dot(a, a))
    if length == 0.0:
        raise ValueError("cannot normalize a zero-length vector")
    return scale(a, 1.0 / length)


def rotate_about_axis(v: Sequence[float], axis: Sequence[float], degrees: float):
    """Rodrigues rotation of ``v`` about a unit ``axis``, right-hand rule."""
    angle = math.radians(degrees)
    c, s = math.cos(angle), math.sin(angle)
    return add(
        add(scale(v, c), scale(cross(axis, v), s)),
        scale(axis, dot(axis, v) * (1.0 - c)),
    )


def vertical_view_axis(eye: Vec3, target: Vec3, up: Vec3) -> Vec3:
    """``up`` with any component along the look direction projected out."""
    forward = normalize(sub(target, eye))
    return normalize(sub(up, scale(forward, dot(up, forward))))


# --- drivers ----------------------------------------------------------------


class CameraOrbit(Driver):
    """Swing the camera about its own vertical view axis, target unchanged.

    Writes both ``camera.eye`` and ``camera.up`` from a single angle. Every
    frame is computed from the reference view rather than nudged incrementally,
    so an angle of 0 reproduces the reference exactly and a long move
    accumulates no drift.
    """

    def __init__(
        self,
        eye: Vec3,
        target: Vec3,
        up: Vec3,
        deg0: float,
        deg1: float,
        start: int,
        end: int,
        ease: Ease = linear,
    ):
        self.eye = tuple(float(x) for x in eye)
        self.target = tuple(float(x) for x in target)
        self.up = tuple(float(x) for x in up)
        self.axis = vertical_view_axis(self.eye, self.target, self.up)
        self.deg0 = float(deg0)
        self.deg1 = float(deg1)
        self.ease = ease
        self.start = int(start)
        self.end = int(end)

    def writes(self) -> Tuple[str, ...]:
        return ("camera.eye", "camera.up")

    def evaluate(self, frame: int) -> Dict[str, Value]:
        u = self.ease(self.progress(frame))
        degrees = self.deg0 + (self.deg1 - self.deg0) * u
        offset = rotate_about_axis(sub(self.eye, self.target), self.axis, degrees)
        return {
            "camera.eye": add(self.target, offset),
            "camera.up": rotate_about_axis(self.up, self.axis, degrees),
        }


# --- segment helpers --------------------------------------------------------


def ramp(channel: str, v0, v1, *, secs=None, frames=None, ease: Ease = linear):
    """Interpolate one channel from ``v0`` to ``v1``. Scalars are promoted."""
    start_value, end_value = as_value(v0), as_value(v1)
    return Segment(
        secs,
        frames,
        lambda s, e: Keyframes(channel, [(s, start_value), (e, end_value)], ease=ease),
    )


def travel_clip_plane(
    normal: Vec3, d0: float, d1: float, *, secs=None, frames=None, ease: Ease = linear
):
    """Slide the clip plane along a fixed normal, from distance d0 to d1."""
    unit = normalize(normal)
    return ramp(
        "clip.plane",
        (*unit, float(d0)),
        (*unit, float(d1)),
        secs=secs,
        frames=frames,
        ease=ease,
    )


def pivot_camera(
    eye: Vec3,
    target: Vec3,
    up: Vec3,
    deg0: float,
    deg1: float,
    *,
    secs=None,
    frames=None,
    ease: Ease = linear,
):
    """Swing the camera about its vertical view axis, from deg0 to deg1."""
    return Segment(
        secs,
        frames,
        lambda s, e: CameraOrbit(eye, target, up, deg0, deg1, s, e, ease=ease),
    )


__all__ = [
    "CameraOrbit",
    "add",
    "cross",
    "dot",
    "lerp",
    "normalize",
    "pivot_camera",
    "ramp",
    "rotate_about_axis",
    "scale",
    "sub",
    "travel_clip_plane",
    "vertical_view_axis",
]
