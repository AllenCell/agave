"""Python API for the in-process headless Vulkan renderer.

This public surface intentionally mirrors ``agave_pyclient.agave``. The method
implementations are independent and pass typed values directly through
nanobind; there is no WebSocket or serialized protocol buffer.
"""

import json
import math
from pathlib import Path
from typing import List, Optional, Sequence

import numpy
from PIL import Image

from . import _native
from .commandbuffer import COMMANDS


def lerp(startframe, endframe, startval, endval):
    x = numpy.linspace(
        startframe, endframe, num=endframe - startframe + 1, endpoint=True
    )
    y = startval + (endval - startval) * (x - startframe) / (endframe - startframe)
    print(y)


def rotation_matrix(axis, theta):
    axis = numpy.asarray(axis)
    axis = axis / math.sqrt(numpy.dot(axis, axis))
    a = math.cos(theta / 2.0)
    b, c, d = -axis * math.sin(theta / 2.0)
    aa, bb, cc, dd = a * a, b * b, c * c, d * d
    bc, ad, ac, ab, bd, cd = b * c, a * d, a * c, a * b, b * d, c * d
    return numpy.array(
        [
            [aa + bb - cc - dd, 2 * (bc + ad), 2 * (bd - ac)],
            [2 * (bc - ad), aa + cc - bb - dd, 2 * (cd + ab)],
            [2 * (bd + ac), 2 * (cd - ab), aa + dd - bb - cc],
        ]
    )


def rotate_vec(vector, axis, angle):
    return numpy.dot(rotation_matrix(axis, angle), vector)


def vec_sub(v1, v2):
    return [v1[i] - v2[i] for i in range(3)]


def vec_add(v1, v2):
    return [v1[i] + v2[i] for i in range(3)]


def vec_normalize(vector):
    magnitude = math.sqrt(sum(value * value for value in vector))
    return [value / magnitude for value in vector]


def vec_cross(v1, v2):
    return [
        v1[1] * v2[2] - v1[2] * v2[1],
        v1[2] * v2[0] - v1[0] * v2[2],
        v1[0] * v2[1] - v1[1] * v2[0],
    ]


def get_vertical_axis(lookdir, up):
    eye_direction = vec_normalize(lookdir)
    object_up = vec_normalize(up)
    sideways = vec_normalize(vec_cross(object_up, eye_direction))
    return vec_normalize(vec_cross(sideways, lookdir))


class AgaveRenderer:
    """Synchronous, headless Vulkan implementation of the agave_pyclient API."""

    def __init__(
        self,
        url: str = "ws://localhost:1235/",
        mode: str = "pathtrace",
        agave_path: Optional[str] = None,
        auto_launch: bool = True,
        launch_retries: int = 10,
        launch_retry_delay: float = 1.0,
    ) -> None:
        del url, agave_path, auto_launch, launch_retries, launch_retry_delay
        if mode not in ("pathtrace", "raymarch"):
            mode = "pathtrace"
        asset_path = Path(_native.__file__).resolve().parent / "assets"
        self._renderer = _native.PythonRenderer(mode, str(asset_path), 0)
        self.session_name = ""

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def close(self) -> None:
        if self._renderer is not None:
            self._renderer.close()
            self._renderer = None

    def _execute(self, command, *args):
        if self._renderer is None:
            raise RuntimeError("Renderer is closed")
        result = self._renderer.execute(COMMANDS[command], *args)
        if isinstance(result, str):
            return json.loads(result)
        return result

    def session(self, name: str):
        self.session_name = name
        return self._execute("SESSION", name)

    def asset_path(self, name: str):
        return self._execute("ASSET_PATH", name)

    def load_ome_tif(self, name: str):
        return self._execute("LOAD_OME_TIF", name)

    def eye(self, x: float, y: float, z: float):
        return self._execute("EYE", x, y, z)

    def target(self, x: float, y: float, z: float):
        return self._execute("TARGET", x, y, z)

    def up(self, x: float, y: float, z: float):
        return self._execute("UP", x, y, z)

    def aperture(self, x: float):
        return self._execute("APERTURE", x)

    def camera_projection(self, projection_type: int, x: float):
        return self._execute("CAMERA_PROJECTION", projection_type, x)

    def focaldist(self, x: float):
        return self._execute("FOCALDIST", x)

    def exposure(self, x: float):
        return self._execute("EXPOSURE", x)

    def mat_diffuse(self, channel: int, r: float, g: float, b: float, a: float):
        return self._execute("MAT_DIFFUSE", channel, r, g, b, a)

    def mat_specular(self, channel: int, r: float, g: float, b: float, a: float):
        return self._execute("MAT_SPECULAR", channel, r, g, b, a)

    def mat_emissive(self, channel: int, r: float, g: float, b: float, a: float):
        return self._execute("MAT_EMISSIVE", channel, r, g, b, a)

    def render_iterations(self, x: int):
        return self._execute("RENDER_ITERATIONS", x)

    def stream_mode(self, x: int):
        return self._execute("STREAM_MODE", x)

    def redraw(self):
        width, height, pixels = self._execute("REDRAW")
        image = Image.frombytes("RGBA", (width, height), pixels, "raw", "BGRA")
        image.save(self.session_name)
        self.session_name = ""

    def set_resolution(self, x: int, y: int):
        return self._execute("SET_RESOLUTION", x, y)

    def density(self, x: float):
        return self._execute("DENSITY", x)

    def frame_scene(self):
        return self._execute("FRAME_SCENE")

    def mat_glossiness(self, channel: int, glossiness: float):
        return self._execute("MAT_GLOSSINESS", channel, glossiness)

    def enable_channel(self, channel: int, enabled: int):
        return self._execute("ENABLE_CHANNEL", channel, enabled)

    def set_window_level(self, channel: int, window: float, level: float):
        return self._execute("SET_WINDOW_LEVEL", channel, window, level)

    def orbit_camera(self, theta: float, phi: float):
        return self._execute("ORBIT_CAMERA", theta, phi)

    def trackball_camera(self, theta: float, phi: float):
        return self._execute("TRACKBALL_CAMERA", theta, phi)

    def skylight_top_color(self, r: float, g: float, b: float):
        return self._execute("SKYLIGHT_TOP_COLOR", r, g, b)

    def skylight_middle_color(self, r: float, g: float, b: float):
        return self._execute("SKYLIGHT_MIDDLE_COLOR", r, g, b)

    def skylight_bottom_color(self, r: float, g: float, b: float):
        return self._execute("SKYLIGHT_BOTTOM_COLOR", r, g, b)

    def light_pos(self, index: int, r: float, theta: float, phi: float):
        return self._execute("LIGHT_POS", index, r, theta, phi)

    def light_color(self, index: int, r: float, g: float, b: float):
        return self._execute("LIGHT_COLOR", index, r, g, b)

    def light_size(self, index: int, x: float, y: float):
        return self._execute("LIGHT_SIZE", index, x, y)

    def set_clip_region(
        self,
        minx: float,
        maxx: float,
        miny: float,
        maxy: float,
        minz: float,
        maxz: float,
    ):
        return self._execute("SET_CLIP_REGION", minx, maxx, miny, maxy, minz, maxz)

    def set_voxel_scale(self, x: float, y: float, z: float):
        return self._execute("SET_VOXEL_SCALE", x, y, z)

    def auto_threshold(self, channel: int, method: int):
        return self._execute("AUTO_THRESHOLD", channel, method)

    def set_percentile_threshold(self, channel: int, pct_low: float, pct_high: float):
        return self._execute("SET_PERCENTILE_THRESHOLD", channel, pct_low, pct_high)

    def mat_opacity(self, channel: int, opacity: float):
        return self._execute("MAT_OPACITY", channel, opacity)

    def set_primary_ray_step_size(self, step_size: float):
        return self._execute("SET_PRIMARY_RAY_STEP_SIZE", step_size)

    def set_secondary_ray_step_size(self, step_size: float):
        return self._execute("SET_SECONDARY_RAY_STEP_SIZE", step_size)

    def background_color(self, r: float, g: float, b: float):
        return self._execute("BACKGROUND_COLOR", r, g, b)

    def set_isovalue_threshold(self, channel: int, isovalue: float, isorange: float):
        return self._execute("SET_ISOVALUE_THRESHOLD", channel, isovalue, isorange)

    def set_control_points(self, channel: int, data: List[float]):
        return self._execute("SET_CONTROL_POINTS", channel, data)

    def load_volume_from_file(self, path: str, scene: int, time: int):
        return self._execute("LOAD_VOLUME_FROM_FILE", path, scene, time)

    def set_time(self, time: int):
        return self._execute("SET_TIME", time)

    def bounding_box_color(self, r: float, g: float, b: float):
        return self._execute("SET_BOUNDING_BOX_COLOR", r, g, b)

    def show_bounding_box(self, on: int):
        return self._execute("SHOW_BOUNDING_BOX", on)

    def load_data(
        self,
        path: str,
        scene: int = 0,
        multiresolution_level: int = 0,
        time: int = 0,
        channels: List[int] = [],
        region: List[int] = [],
    ):
        return self._execute(
            "LOAD_DATA", path, scene, multiresolution_level, time, channels, region
        )

    def load_data_and_get_info(
        self,
        path: str,
        scene: int = 0,
        multiresolution_level: int = 0,
        time: int = 0,
        channels: List[int] = [],
        region: List[int] = [],
    ) -> dict:
        return self.load_data(
            path, scene, multiresolution_level, time, channels, region
        )

    def load_array(
        self,
        data: numpy.ndarray,
        name: str = "array",
        voxel_size: Sequence[float] = (1.0, 1.0, 1.0),
        spatial_units: str = "units",
        channel_names: Optional[Sequence[str]] = None,
    ) -> dict:
        """Replace the current volume with a copied ZYX or CZYX NumPy array."""
        if self._renderer is None:
            raise RuntimeError("Renderer is closed")

        array = numpy.asarray(data)
        if array.dtype not in (
            numpy.dtype("uint8"),
            numpy.dtype("uint16"),
            numpy.dtype("float32"),
        ):
            raise TypeError("data dtype must be uint8, uint16, or float32")
        if array.ndim not in (3, 4):
            raise ValueError("data must have ZYX or CZYX shape")

        result = self._renderer.load_array(
            numpy.ascontiguousarray(array),
            name,
            list(voxel_size),
            spatial_units,
            list(channel_names) if channel_names is not None else [],
        )
        return json.loads(result)

    def show_scale_bar(self, on: int):
        return self._execute("SHOW_SCALE_BAR", on)

    def set_flip_axis(self, x: int, y: int, z: int):
        return self._execute("SET_FLIP_AXIS", x, y, z)

    def set_interpolation(self, x: int):
        return self._execute("SET_INTERPOLATION", x)

    def set_clip_plane(self, x: float, y: float, z: float, d: float):
        return self._execute("SET_CLIP_PLANE", x, y, z, d)

    def set_color_ramp(self, channel: int, name: str, data: List[float]):
        return self._execute("SET_COLOR_RAMP", channel, name, data)

    def set_min_max_threshold(self, channel: int, min_val: int, max_val: int):
        return self._execute("SET_MIN_MAX_THRESHOLD", channel, min_val, max_val)

    def set_skylight_rotation(self, x: float, y: float, z: float, w: float):
        return self._execute("SET_SKYLIGHT_ROTATION", x, y, z, w)

    def show_time_stamp(self, on: int):
        return self._execute("SHOW_TIME_STAMP", on)

    def set_time_stamp_format(self, format: int):
        return self._execute("SET_TIME_STAMP_FORMAT", format)

    def set_multichannel_blend(self, mode: int):
        """Set Vulkan path-tracer channel blending: 0 for Max, 1 for Weighted."""
        return self._execute("SET_MULTICHANNEL_BLEND", mode)

    def batch_render_turntable(
        self, number_of_frames=90, direction=1, output_name="frame", first_frame=0
    ):
        if direction not in (1, -1):
            return
        for i in range(number_of_frames):
            self.session(f"{output_name}_{i + first_frame}.png")
            self.redraw()
            self.trackball_camera(0.0, direction * (360.0 / float(number_of_frames)))

    def batch_render_rocker(
        self,
        number_of_frames=90,
        angle=30,
        direction=1,
        output_name="frame",
        first_frame=0,
    ):
        if direction not in (1, -1):
            return
        angle_delta = 4.0 * float(angle) / float(number_of_frames)
        for i in range(number_of_frames):
            quadrant = (i * 4) // number_of_frames
            quadrant_direction = 1 if quadrant in (0, 3) else -1
            self.session(f"{output_name}_{i + first_frame}.png")
            self.redraw()
            self.trackball_camera(0.0, angle_delta * direction * quadrant_direction)


__all__ = ["AgaveRenderer"]
