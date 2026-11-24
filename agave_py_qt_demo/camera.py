"""Python-owned camera for an AgaveCanvas, mirroring renderlib's CCamera.

The demo keeps each tile's view orientation here and sends the resulting
eye/target/up to the canvas, so a reload (which resets the native camera to
the front view) never loses the view.
"""

import math
from dataclasses import dataclass

import numpy

# renderlib DEF_CAMERA_FIELD_OF_VIEW (vertical, degrees).
FOV_DEG = 55.0


@dataclass(frozen=True)
class Orientation:
    # Unit vector from the target toward the eye, and the camera up vector.
    direction: tuple = (0.0, 0.0, 1.0)
    up: tuple = (0.0, 1.0, 0.0)


FRONT = Orientation()


def _rotate(v: numpy.ndarray, degrees: float, axis: numpy.ndarray) -> numpy.ndarray:
    """Rodrigues rotation of v about axis, matching glm::rotate."""
    axis = axis / numpy.linalg.norm(axis)
    a = math.radians(degrees)
    return (
        v * math.cos(a)
        + numpy.cross(axis, v) * math.sin(a)
        + axis * numpy.dot(axis, v) * (1.0 - math.cos(a))
    )


def orbit(o: Orientation, down_deg: float, right_deg: float) -> Orientation:
    """Same rotation as CCamera::Orbit / the ORBIT_CAMERA command."""
    reverse = numpy.array(o.direction, dtype=float)
    up = numpy.array(o.up, dtype=float)
    world_up = numpy.array((0.0, 1.0, 0.0))
    right = numpy.cross(up, reverse)
    if numpy.linalg.norm(right) > 1e-9:
        reverse = _rotate(reverse, down_deg, right)
        up = _rotate(up, down_deg, right)
    reverse = _rotate(reverse, right_deg, world_up)
    up = _rotate(up, right_deg, world_up)
    return Orientation(
        tuple(reverse / numpy.linalg.norm(reverse)), tuple(up / numpy.linalg.norm(up))
    )


def pose(physical_xyz: tuple, o: Orientation) -> tuple:
    """(eye, target, up) framing the volume like CCamera::SetViewMode.

    renderlib places the volume in a box from 0 to its physical size divided
    by the largest axis, and backs the eye off so the box diagonal fits FOV_DEG.
    """
    physical = numpy.array(physical_xyz, dtype=float)
    dims = physical / physical.max()
    target = dims * 0.5
    distance = numpy.linalg.norm(dims) * 0.5 / math.tan(math.radians(FOV_DEG) * 0.5)
    eye = target + numpy.array(o.direction) * distance
    return tuple(eye), tuple(target), tuple(o.up)
