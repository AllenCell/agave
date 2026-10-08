# require pillow, numpy, ws4py
import math
import numpy
from PIL import Image
from typing import List, Optional

from .agave_client import AgaveClient
from .commandbuffer import CommandBuffer
from .find_agave import (
    find_matching_subdirectories,
    guess_agave_path,
    launch_agave_process,
    port_from_url,
    resolve_agave_path,
    wait_for_connection,
)

__all__ = [
    "AgaveClient",
    "AgaveRenderer",
    "find_matching_subdirectories",
    "guess_agave_path",
]


def lerp(startframe, endframe, startval, endval):
    x = numpy.linspace(
        startframe, endframe, num=endframe - startframe + 1, endpoint=True
    )
    y = startval + (endval - startval) * (x - startframe) / (endframe - startframe)
    print(y)


def rotation_matrix(axis, theta):
    """
    Return the rotation matrix associated with counterclockwise rotation about
    the given axis by theta radians.
    """
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


def rotate_vec(v, axis, angle):
    return numpy.dot(rotation_matrix(axis, angle), v)


def vec_sub(v1, v2):
    return [v1[0] - v2[0], v1[1] - v2[1], v1[2] - v2[2]]


def vec_add(v1, v2):
    return [v1[0] + v2[0], v1[1] + v2[1], v1[2] + v2[2]]


def vec_normalize(v):
    vmag = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    return [v[0] / vmag, v[1] / vmag, v[2] / vmag]


def vec_cross(v1, v2):
    c = [
        v1[1] * v2[2] - v1[2] * v2[1],
        v1[2] * v2[0] - v1[0] * v2[2],
        v1[0] * v2[1] - v1[1] * v2[0],
    ]
    return c


def get_vertical_axis(lookdir, up):
    eyeDirection = vec_normalize(lookdir)
    objectUpDirection = vec_normalize(up)
    objectSidewaysDirection = vec_normalize(vec_cross(objectUpDirection, eyeDirection))
    axis = vec_normalize(vec_cross(objectSidewaysDirection, lookdir))
    return axis


class AgaveRenderer:
    """
    AgaveRenderer communicates with AGAVE running in server mode to perform GPU volume
    rendering.

    Parameters
    ----------
    url: str
        Full url to websocket server including port.
    mode: str
        "pathtrace" or "raymarch" (pathtrace is default).
    agave_path: Optional[str]
        Path to an AGAVE executable to launch if no running server is found.
        If ``None``, a standard install location will be searched for via
        :func:`guess_agave_path`. Ignored when ``auto_launch`` is False.
    auto_launch: bool
        If True (default), and no AGAVE instance can be reached at ``url``,
        attempt to launch one locally. If False, only connect to an already
        running instance and raise on failure.
    launch_retries: int
        Number of times to retry connecting after launching AGAVE.
    launch_retry_delay: float
        Seconds to wait between connection retries after launching AGAVE.

    Examples
    --------
    Connect to a running local AGAVE server, launching one if necessary:

    >>> agaveclient = AgaveRenderer()

    Connect only — do not auto-launch:

    >>> agaveclient = AgaveRenderer(auto_launch=False)

    Launch a specific AGAVE executable:

    >>> agaveclient = AgaveRenderer(agave_path="/path/to/agave")

    Notes
    -----
    Connection precedence (highest to lowest):

    1. An AGAVE server already listening at ``url`` is always used
       first, regardless of the other arguments. ``agave_path`` and
       ``auto_launch`` are ignored when the initial connect succeeds.

    2. If no server responds and ``auto_launch`` is False, the
       connection error is re-raised immediately. ``agave_path`` is
       not consulted.

    3. If no server responds and ``auto_launch`` is True, an AGAVE
       executable is located using:

       a. ``agave_path`` if explicitly provided, else
       b. :func:`guess_agave_path` (PATH, then standard install
          locations for the current OS).

       The executable is launched with ``--server --port=<port>``
       where ``<port>`` is parsed from ``url``, then the connection
       is retried up to ``launch_retries`` times spaced
       ``launch_retry_delay`` seconds apart.
    """

    def __init__(
        self,
        url: str = "ws://localhost:1235/",
        mode: str = "pathtrace",
        agave_path: Optional[str] = None,
        auto_launch: bool = True,
        launch_retries: int = 10,
        launch_retry_delay: float = 1.0,
    ) -> None:
        # See class docstring "Notes" section for the connection-precedence
        # rules that this constructor implements.
        self.agave_process = None
        self.cb = CommandBuffer()
        self.session_name = ""
        if mode != "pathtrace" and mode != "raymarch":
            mode = "pathtrace"
        self._url = url
        self._mode = mode

        # 1. Try to connect to an already-running AGAVE instance.
        try:
            self._connect()
            return
        except Exception as connect_err:
            if not auto_launch:
                raise
            print(
                f"Could not connect to AGAVE at {url} ({connect_err}); "
                "attempting to launch a local instance..."
            )

        # 2. Locate an AGAVE executable and launch it in server mode.
        path = resolve_agave_path(agave_path)
        port = port_from_url(url)
        self.agave_process = launch_agave_process(path, port=port)

        # 3. Retry connecting until the server is up.
        try:
            wait_for_connection(
                self._connect,
                self.agave_process,
                retries=launch_retries,
                retry_delay=launch_retry_delay,
            )
        except Exception:
            # wait_for_connection has already terminated the process on failure.
            self.agave_process = None
            raise

    def _connect(self) -> None:
        """Open a websocket connection to the AGAVE server."""
        self.ws = AgaveClient(
            f"{self._url}?mode={self._mode}", protocols=["http-only", "chat"]
        )
        self.ws.connect()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def close(self) -> None:
        """Close the websocket and any AGAVE process started by this client."""
        try:
            if getattr(self, "ws", None) is not None:
                self.ws.close()
        except Exception:
            pass
        if self.agave_process is not None:
            try:
                self.agave_process.terminate()
                self.agave_process.wait()
            except Exception:
                pass
            self.agave_process = None

    def session(self, name: str):
        """
        Set the current session name.  Use the full path to the name of the output
        image here.

        Parameters
        ----------
        name: str
            This name is the full path to the output image, ending in .png or .jpg.
            Make sure the directory has already been created.
        """
        # 0
        self.cb.add_command("SESSION", name)
        self.session_name = name

    def asset_path(self, name: str):
        """
        Sets a search path for volume files. NOT YET IMPLEMENTED.

        Parameters
        ----------
        name: str
            This name is the path where volume images are located.
        """
        # 1
        self.cb.add_command("ASSET_PATH", name)

    def load_ome_tif(self, name: str):
        """
        DEPRECATED. Use load_data
        """
        # 2
        self.cb.add_command("LOAD_OME_TIF", name)

    def eye(self, x: float, y: float, z: float):
        """
        Set the viewer camera position.
        Default is (500,500,500).

        Parameters
        ----------
        x: float
            The x coordinate
        y: float
            The y coordinate
        z: float
            The z coordinate
        """
        # 3
        self.cb.add_command("EYE", x, y, z)

    def target(self, x: float, y: float, z: float):
        """
        Set the viewer target position. This is a point toward which we are looking.
        Default is (0,0,0).

        Parameters
        ----------
        x: float
            The x coordinate
        y: float
            The y coordinate
        z: float
            The z coordinate
        """
        # 4
        self.cb.add_command("TARGET", x, y, z)

    def up(self, x: float, y: float, z: float):
        """
        Set the viewer camera up direction.  This is a vector which should be nearly
        perpendicular to the view direction (target-eye), and defines the "roll" amount
        for the camera.
        Default is (0,0,1).

        Parameters
        ----------
        x: float
            The x coordinate
        y: float
            The y coordinate
        z: float
            The z coordinate
        """
        # 5
        self.cb.add_command("UP", x, y, z)

    def aperture(self, x: float):
        """
        Set the viewer camera aperture size.

        Parameters
        ----------
        x: float
            The aperture size.
            This is a number between 0 and 1. 0 means no defocusing will occur, like a
            pinhole camera.  1 means maximum defocus. Default is 0.
        """
        # 6
        self.cb.add_command("APERTURE", x)

    def camera_projection(self, projection_type: int, x: float):
        """
        Set the viewer camera projection type, along with a relevant parameter.

        Parameters
        ----------
        projection_type: int
            0 for Perspective, 1 for Orthographic.  Default: 0
        x: float
            If Perspective, then this is the vertical Field of View angle in degrees.
            If Orthographic, then this is the orthographic scale dimension.
            Default: 55.0 degrees. (default Orthographic scale is 0.5)
        """
        # 7
        self.cb.add_command("CAMERA_PROJECTION", projection_type, x)

    def focaldist(self, x: float):
        """
        Set the viewer camera focal distance

        Parameters
        ----------
        x: float
            The focal distance.  Has no effect if aperture is 0.
        """
        # 8
        self.cb.add_command("FOCALDIST", x)

    def exposure(self, x: float):
        """
        Set the exposure level

        Parameters
        ----------
        x: float
            The exposure level between 0 and 1. Default is 0.75.  Higher numbers are
            brighter.
        """
        # 9
        self.cb.add_command("EXPOSURE", x)

    def mat_diffuse(self, channel: int, r: float, g: float, b: float, a: float):
        """
        Set the diffuse color of a channel

        Parameters
        ----------
        channel: int
            Which channel index, 0 based.
        r: float
            The red value between 0 and 1
        g: float
            The green value between 0 and 1
        b: float
            The blue value between 0 and 1
        a: float
            The alpha value between 0 and 1 (currently unused)
        """
        # 10
        self.cb.add_command("MAT_DIFFUSE", channel, r, g, b, a)

    def mat_specular(self, channel: int, r: float, g: float, b: float, a: float):
        """
        Set the specular color of a channel (defaults to black, for no specular
        response)

        Parameters
        ----------
        channel: int
            Which channel index, 0 based.
        r: float
            The red value between 0 and 1
        g: float
            The green value between 0 and 1
        b: float
            The blue value between 0 and 1
        a: float
            The alpha value between 0 and 1 (currently unused)
        """
        # 11
        self.cb.add_command("MAT_SPECULAR", channel, r, g, b, a)

    def mat_emissive(self, channel: int, r: float, g: float, b: float, a: float):
        """
        Set the emissive color of a channel (defaults to black, for no emission)

        Parameters
        ----------
        channel: int
            Which channel index, 0 based.
        r: float
            The red value between 0 and 1
        g: float
            The green value between 0 and 1
        b: float
            The blue value between 0 and 1
        a: float
            The alpha value between 0 and 1 (currently unused)
        """
        # 12
        self.cb.add_command("MAT_EMISSIVE", channel, r, g, b, a)

    def render_iterations(self, x: int):
        """
        Set the number of paths per pixel to accumulate.

        Parameters
        ----------
        x: int
            How many paths per pixel. The more paths, the less noise in the image.
        """
        # 13
        self.cb.add_command("RENDER_ITERATIONS", x)

    def stream_mode(self, x: int):
        """
        Turn stream mode on or off.  Stream mode will send an image back to the client
        on each iteration up to some server-defined amount.  This mode is useful for
        interactive client-server applications but not for batch-mode offline rendering.

        Parameters
        ----------
        x: int
            0 for off, 1 for on. Default is off.
        """
        # 14
        self.cb.add_command("STREAM_MODE", x)

    def redraw(self):
        """
        Tell the server to process all commands and return an image, and then save the
        image.  This function will block and wait for the image to be returned.
        The image returned will be saved automatically using the session_name.
        TODO: a timeout is not yet implemented.
        """
        # 15
        # issue command buffer
        self.cb.add_command("REDRAW")
        buf = self.cb.make_buffer()
        # TODO ENSURE CONNECTED
        self.ws.send(buf, True)
        #  and then WAIT for render to be completed
        binarydata = self.ws.wait_for_image()
        # and save image
        im = Image.open(binarydata)
        print(self.session_name)
        im.save(self.session_name)
        # ready for next frame
        self.session_name = ""
        self.cb = CommandBuffer()

    def set_resolution(self, x: int, y: int):
        """
        Set the image resolution in pixels.

        Parameters
        ----------
        x: int
            x resolution in pixels
        y: int
            y resolution in pixels
        """
        # 16
        self.cb.add_command("SET_RESOLUTION", x, y)

    def density(self, x: float):
        """
        Set the scattering density.

        Parameters
        ----------
        x: float
            The scattering density, 0-100.  Higher values will make the volume seem
            more opaque. Default is 8.5, which is relatively transparent.
        """
        # 17
        self.cb.add_command("DENSITY", x)

    def frame_scene(self):
        """
        Automatically set camera parameters so that the volume fills the view.
        Useful when you have insufficient information to position the camera accurately.
        """
        # 18
        self.cb.add_command("FRAME_SCENE")

    def mat_glossiness(self, channel: int, glossiness: float):
        """
        Set the channel's glossiness.

        Parameters
        ----------
        channel: int
            Which channel index, 0 based.
        glossiness: float
            Sets the shininess, a number between 0 and 100.
        """
        # 19
        self.cb.add_command("MAT_GLOSSINESS", channel, glossiness)

    def enable_channel(self, channel: int, enabled: int):
        """
        Show or hide a given channel

        Parameters
        ----------
        channel: int
            Which channel index, 0 based.
        enabled: int
            0 to hide, 1 to show
        """
        # 20
        self.cb.add_command("ENABLE_CHANNEL", channel, enabled)

    def set_window_level(self, channel: int, window: float, level: float):
        """
        Set intensity threshold for a given channel based on Window/Level

        Parameters
        ----------
        channel: int
            Which channel index, 0 based.
        window: float
            Width of the window, from 0-1.
        level: float
            Intensity level mapped to middle of window, from 0-1
        """
        # 21
        self.cb.add_command("SET_WINDOW_LEVEL", channel, window, level)

    def orbit_camera(self, theta: float, phi: float):
        """
        Rotate the camera around the volume by angle deltas

        Parameters
        ----------
        theta: float
            polar angle in degrees
        phi: float
            azimuthal angle in degrees
        """
        # 22
        self.cb.add_command("ORBIT_CAMERA", theta, phi)

    def trackball_camera(self, theta: float, phi: float):
        """
        Rotate the camera around the volume by angle deltas

        Parameters
        ----------
        theta: float
            vertical screen angle in degrees
        phi: float
            horizontal screen angle in degrees
        """
        # 43
        self.cb.add_command("TRACKBALL_CAMERA", theta, phi)

    def skylight_top_color(self, r: float, g: float, b: float):
        """
        Set the "north pole" color of the sky sphere

        Parameters
        ----------
        r: float
            The red value between 0 and 1
        g: float
            The green value between 0 and 1
        b: float
            The blue value between 0 and 1
        """
        # 23
        self.cb.add_command("SKYLIGHT_TOP_COLOR", r, g, b)

    def skylight_middle_color(self, r: float, g: float, b: float):
        """
        Set the "equator" color of the sky sphere

        Parameters
        ----------
        r: float
            The red value between 0 and 1
        g: float
            The green value between 0 and 1
        b: float
            The blue value between 0 and 1
        """
        # 24
        self.cb.add_command("SKYLIGHT_MIDDLE_COLOR", r, g, b)

    def skylight_bottom_color(self, r: float, g: float, b: float):
        """
        Set the "south pole" color of the sky sphere

        Parameters
        ----------
        r: float
            The red value between 0 and 1
        g: float
            The green value between 0 and 1
        b: float
            The blue value between 0 and 1
        """
        # 25
        self.cb.add_command("SKYLIGHT_BOTTOM_COLOR", r, g, b)

    def light_pos(self, index: int, r: float, theta: float, phi: float):
        """
        Set the position of an area light, in spherical coordinates

        Parameters
        ----------
        index: int
            Which light to set.  Currently unused as there is only one area light.
        r: float
            The radius, as distance from the center of the volume
        theta: float
            The polar angle
        phi: float
            The azimuthal angle
        """
        # 26
        self.cb.add_command("LIGHT_POS", index, r, theta, phi)

    def light_color(self, index: int, r: float, g: float, b: float):
        """
        Set the color of an area light. Overdrive the values higher than 1 to increase
        the light's intensity.

        Parameters
        ----------
        index: int
            Which light to set.  Currently unused as there is only one area light.
        r: float
            The red value between 0 and 1
        g: float
            The green value between 0 and 1
        b: float
            The blue value between 0 and 1
        """
        # 27
        self.cb.add_command("LIGHT_COLOR", index, r, g, b)

    def light_size(self, index: int, x: float, y: float):
        """
        Set the size dimensions of a rectangular area light.

        Parameters
        ----------
        index: int
            Which light to set.  Currently unused as there is only one area light.
        x: float
            The width dimension of the area light
        y: float
            The height dimension of the area light
        """
        # 28
        self.cb.add_command("LIGHT_SIZE", index, x, y)

    def set_clip_region(
        self,
        minx: float,
        maxx: float,
        miny: float,
        maxy: float,
        minz: float,
        maxz: float,
    ):
        """
        Set the axis aligned region of interest of the volume. All axis values are
        relative, where 0 is one extent of the volume and 1 is the opposite extent.
        For example, (0,1, 0,1, 0,0.5) will select the lower half of the volume's z
        slices.

        Parameters
        ----------
        minx: float
            The lower x extent between 0 and 1
        maxx: float
            The higher x extent between 0 and 1
        miny: float
            The lower y extent between 0 and 1
        maxy: float
            The higher y extent between 0 and 1
        minz: float
            The lower z extent between 0 and 1
        maxz: float
            The higher z extent between 0 and 1
        """
        # 29
        self.cb.add_command("SET_CLIP_REGION", minx, maxx, miny, maxy, minz, maxz)

    def set_voxel_scale(self, x: float, y: float, z: float):
        """
        Set the relative scale of the pixels in the volume. Typically this is filled in
        with the physical pixel dimensions from the microscope metadata.  Often the x
        and y scale will differ from the z scale.  Defaults to (1,1,1)

        Parameters
        ----------
        x: float
            x scale
        y: float
            y scale
        z: float
            z scale
        """
        # 30
        self.cb.add_command("SET_VOXEL_SCALE", x, y, z)

    def auto_threshold(self, channel: int, method: int):
        """
        Automatically determine the intensity thresholds

        Parameters
        ----------
        channel: int
            Which channel index, 0 based.
        method: int
            Allowed values:
            0: Auto2
            1: Auto
            2: BestFit
            3: ChimeraX emulation
            4: between 0.5 percentile and 0.98 percentile
        """
        # 31
        self.cb.add_command("AUTO_THRESHOLD", channel, method)

    def set_percentile_threshold(self, channel: int, pct_low: float, pct_high: float):
        """
        Set intensity thresholds based on percentiles of pixels to clip min and max
        intensity

        Parameters
        ----------
        channel: int
            Which channel index, 0 based.
        pct_low: float
            The low percentile to remap to 0(min) intensity
        pct_high: float
            The high percentile to remap to 1(max) intensity
        """
        # 32
        self.cb.add_command("SET_PERCENTILE_THRESHOLD", channel, pct_low, pct_high)

    def mat_opacity(self, channel: int, opacity: float):
        """
        Set channel opacity. This is a multiplier against all intensity values in the
        channel.

        Parameters
        ----------
        channel: int
            Which channel index, 0 based.
        opacity: float
            A multiplier between 0 and 100. Default is 1
        """
        # 33
        self.cb.add_command("MAT_OPACITY", channel, opacity)

    def set_primary_ray_step_size(self, step_size: float):
        """
        Set primary ray step size. This is an accuracy versus speed tradeoff.  Low
        values are more accurate. High values will render faster.
        Primary rays are the rays that are cast from the camera out into the volume.

        Parameters
        ----------
        step_size: float
            A value in voxels. Default is 4.  Minimum sensible value is 1.
        """
        # 34
        self.cb.add_command("SET_PRIMARY_RAY_STEP_SIZE", step_size)

    def set_secondary_ray_step_size(self, step_size: float):
        """
        Set secondary ray step size. This is an accuracy versus speed tradeoff.  Low
        values are more accurate. High values will render faster.
        The secondary rays are rays which are cast toward lights after they have
        scattered within the volume.

        Parameters
        ----------
        step_size: float
            A value in voxels. Default is 4.  Minimum sensible value is 1.
        """
        # 35
        self.cb.add_command("SET_SECONDARY_RAY_STEP_SIZE", step_size)

    def background_color(self, r: float, g: float, b: float):
        """
        Set the background color of the rendering

        Parameters
        ----------
        r: float
            The red value between 0 and 1
        g: float
            The green value between 0 and 1
        b: float
            The blue value between 0 and 1
        """
        # 36
        self.cb.add_command("BACKGROUND_COLOR", r, g, b)

    def set_isovalue_threshold(self, channel: int, isovalue: float, isorange: float):
        """
        Set intensity thresholds based on values around an isovalue.

        Parameters
        ----------
        channel: int
            Which channel index, 0 based.
        isovalue: float
            The value to center at maximum intensity, between 0 and 1
        isorange: float
            A range around the isovalue to keep at constant intensity, between 0 and 1.
            Typically small, to select for a single isovalue.
        """
        # 37
        self.cb.add_command("SET_ISOVALUE_THRESHOLD", channel, isovalue, isorange)

    def set_control_points(self, channel: int, data: List[float]):
        """
        Set intensity thresholds based on a piecewise linear transfer function.

        Parameters
        ----------
        channel: int
            Which channel index, 0 based.
        data: List[float]
            An array of values.  5 floats per control point.  first is position (0-1),
            next four are rgba (all 0-1).  Only alpha is currently used as the remapped
            intensity value.  All others are linearly interpolated.
        """
        # 38
        self.cb.add_command("SET_CONTROL_POINTS", channel, data)

    def load_volume_from_file(self, path: str, scene: int, time: int):
        """
        DEPRECATED. Use load_data
        """
        # 39
        self.cb.add_command("LOAD_VOLUME_FROM_FILE", path, scene, time)

    def set_time(self, time: int):
        """
        Load a time from the current volume file

        Parameters
        ----------
        time: int
            zero-based index to select the time sample.  Defaults to 0
        """
        # 40
        self.cb.add_command("SET_TIME", time)

    def bounding_box_color(self, r: float, g: float, b: float):
        """
        Set the color for the bounding box display

        Parameters
        ----------
        r: float
            the red value, from 0 to 1
        g: float
            the green value, from 0 to 1
        b: float
            the blue value, from 0 to 1
        """
        # 41
        self.cb.add_command("SET_BOUNDING_BOX_COLOR", r, g, b)

    def show_bounding_box(self, on: int):
        """
        Turn bounding box display on or off

        Parameters
        ----------
        on: int
            0 to hide bounding box, 1 to show it
        """
        # 42
        self.cb.add_command("SHOW_BOUNDING_BOX", on)

    def load_data(
        self,
        path: str,
        scene: int = 0,
        multiresolution_level: int = 0,
        time: int = 0,
        channels: List[int] = [],
        region: List[int] = [],
    ):
        """
        Completely specify volume data to load

        Parameters
        ----------
        path: str
            URL or directory or file path to the data. The path must be locally
            accessible from the AGAVE server.

        scene: int
            zero-based index to select the scene, for multi-scene files. Defaults to 0

        multiresolution_level: int
            zero-based index to select the multiresolution level.  Defaults to 0

        time: int
            zero-based index to select the time sample.  Defaults to 0

        channels: List[int]
            zero-based indices to select the channels.  Defaults to all channels

        region: List[int]
            6 integers specifying the region to load.  Defaults to the entire volume.
            Any list length other than 0 or 6 is an error.
        """
        # 44
        self.cb.add_command(
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
        """
        Load a volume and return the server's metadata response as a dict.

        Same arguments as :meth:`load_data`, but this variant immediately
        flushes the command buffer to the server, waits for the JSON text
        frame the server sends back, and returns the parsed dictionary.

        The returned dict contains at least the following keys (see
        ``LoadDataCommand::execute`` in ``renderlib/command.cpp``):

        - ``commandId``           : int, id of the LOAD_DATA command
        - ``x``, ``y``, ``z``, ``c``, ``t`` : int, image extents
        - ``pixel_size_x/y/z``    : float, physical voxel size
        - ``channel_names``       : list[str]
        - ``channel_min_intensity``: list[int], one value per channel
        - ``channel_max_intensity``: list[int], one value per channel
        - ``volume_dimensions``   : nested dict mirroring the C++
          ``VolumeDimensions`` struct

        Note: any other commands queued in the buffer prior to calling
        this method will be flushed at the same time.

        Returns
        -------
        dict
            Parsed JSON metadata for the loaded volume.
        """
        self.load_data(path, scene, multiresolution_level, time, channels, region)
        buf = self.cb.make_buffer()
        self.ws.send(buf, True)
        # Reset the buffer for subsequent commands.
        self.cb = CommandBuffer()
        return self.ws.wait_for_json()

    def show_scale_bar(self, on: int):
        """
        Turn scale bar display on or off

        Parameters
        ----------
        on: int
            0 to hide scale bar, 1 to show it
        """
        # 45
        self.cb.add_command("SHOW_SCALE_BAR", on)

    def set_flip_axis(self, x: int, y: int, z: int):
        """
        Flip the volume data on any axis

        Parameters
        ----------
        x: int
            -1 to flip, 1 to not flip
        y: int
            -1 to flip, 1 to not flip
        z: int
            -1 to flip, 1 to not flip
        """
        # 46
        self.cb.add_command("SET_FLIP_AXIS", x, y, z)

    def set_interpolation(self, x: int):
        """
        Set the volume sampling interpolation mode

        Parameters
        ----------
        x: int
            0 for nearest, 1 for linear
        """
        # 47
        self.cb.add_command("SET_INTERPOLATION", x)

    def set_clip_plane(self, x: float, y: float, z: float, d: float):
        """
        Set the clip plane equation.  The xyz vector must be normalized.

        Parameters
        ----------
        x: float
            The x component of the normal
        y: float
            The y component of the normal
        z: float
            The z component of the normal
        d: float
            The distance from the origin
        """
        # 48
        self.cb.add_command("SET_CLIP_PLANE", x, y, z, d)

    def set_color_ramp(self, channel: int, name: str, data: List[float]):
        """
        Set intensity thresholds based on a piecewise linear transfer function.

        Parameters
        ----------
        channel: int
            Which channel index, 0 based.
        name: str
            The name of the color ramp
        data: List[float]
            An array of values.  5 floats per control point.  first is position (0-1),
            next four are rgba (all 0-1).  Currently alpha will be ignored.
            To guarantee correct behavior, the first control point should be at 0.0 and
            the last at 1.0.
        """
        # 49
        self.cb.add_command("SET_COLOR_RAMP", channel, name, data)

    def set_min_max_threshold(self, channel: int, min_val: int, max_val: int):
        """
        Set the minimum and maximum intensity thresholds for a channel.

        Parameters
        ----------
        channel: int
            Which channel index, 0 based.
        min_val: int
            The minimum intensity value.
        max_val: int
            The maximum intensity value.
        """
        # 50
        self.cb.add_command("SET_MIN_MAX_THRESHOLD", channel, min_val, max_val)

    def set_skylight_rotation(self, x: float, y: float, z: float, w: float):
        """
        Set the orientation of the sphere (sky) light as a quaternion. The quaternion
        rotates the default direction (0, 0, 1) to the desired sky-light direction and
        also encodes the roll of the sphere around that direction, which determines
        where the top/middle/bottom color bands appear relative to the volume.

        Parameters
        ----------
        x: float
            Quaternion x component.
        y: float
            Quaternion y component.
        z: float
            Quaternion z component.
        w: float
            Quaternion w component.
        """
        # 51
        self.cb.add_command("SET_SKYLIGHT_ROTATION", x, y, z, w)

    def show_time_stamp(self, on: int):
        """
        Turn timestamp display on or off

        Parameters
        ----------
        on: int
            0 to hide timestamps, 1 to show them
        """
        # 52
        self.cb.add_command("SHOW_TIME_STAMP", on)

    def set_time_stamp_format(self, format: int):
        """
        Set the timestamp display format

        Parameters
        ----------
        format: int
            0 for HH:MM:SS, 1 for time units
        """
        # 53
        self.cb.add_command("SET_TIME_STAMP_FORMAT", format)

    def set_multichannel_blend(self, mode: int):
        """
        Set how enabled channels are combined by the Vulkan path tracer.

        Parameters
        ----------
        mode: int
            0 to use the maximum channel contribution, 1 to use weighted blending.
        """
        # 54
        self.cb.add_command("SET_MULTICHANNEL_BLEND", mode)

    def batch_render_turntable(
        self, number_of_frames=90, direction=1, output_name="frame", first_frame=0
    ):
        """
        Loop to render a turntable sequence, a 360 degree rotation about the vertical
        axis.  Other commands must have been previously issued to load the data and set
        all the viewing parameters.

        Parameters
        ----------
        number_of_frames: int
            How many images to generate
        direction: int
            rotate to the left or to the right, +1 or -1
        output_name: str
            a full path prefix. The file names will have the frame numbers
            automatically appended
        first_frame: int
            an offset for the frame indices in the saved file names
        """
        # direction must be +/-1
        if direction != 1 and direction != -1:
            return

        # then orbit the camera parametrically
        for i in range(0, number_of_frames):
            self.session(f"{output_name}_{i + first_frame}.png")
            self.redraw()
            # first frame gets zero orbit, then onward:
            self.trackball_camera(0.0, direction * (360.0 / float(number_of_frames)))

    def batch_render_rocker(
        self,
        number_of_frames=90,
        angle=30,
        direction=1,
        output_name="frame",
        first_frame=0,
    ):
        """
        Loop to render a rocker sequence, an oscillating partial rotation about the
        vertical axis.  Other commands must have been previously issued to load the
        data and set all the viewing parameters.

        Parameters
        ----------
        number_of_frames: int
            How many images to generate
        angle: float
            Max angle to rock back and forth, in degrees
        direction: int
            rotate to the left or to the right
        output_name: str
            a full path prefix. The file names will have the frame numbers
            automatically appended
        first_frame: int
            an offset for the frame indices in the saved file names
        """
        # direction must be +/-1
        if direction != 1 and direction != -1:
            return

        # orbit the camera parametrically
        angledelta = 4.0 * float(angle) / float(number_of_frames)
        for i in range(0, number_of_frames):
            quadrant = (i * 4) // number_of_frames
            quadrantdirection = 1 if quadrant == 0 or quadrant == 3 else -1
            self.session(f"{output_name}_{i + first_frame}.png")
            self.redraw()
            # first frame gets zero orbit, then onward:
            self.trackball_camera(0.0, angledelta * direction * quadrantdirection)
