# AGAVE Python animation (standalone, headless Vulkan)
#
# 384 frames @ 24 fps = 16 seconds:
#   frames   0- 47 (2 s) : camera pivots 45 deg, nothing else changes
#   frames  48- 95 (2 s) : camera pivots back to the original view
#   frames  96-191 (4 s) : clip plane slides into the volume, camera still
#   frames 192-239 (2 s) : membrane channel fades to 0 opacity, plane held
#   frames 240-335 (4 s) : clip plane walks back to its original position
#   frames 336-383 (2 s) : membrane channel fades back in, plane parked
#
# Frame 0 is the scene exactly as test.py left it: the clip plane starts parked
# at the far edge of the displayed data, so it clips nothing.
#
#   pip install agave_pyvk
#   python test_animation.py
#
# Then encode, e.g.:
#   ffmpeg -framerate 24 -i frames/frame_%04d.png -c:v libx264 -pix_fmt yuv420p out.mp4

import itertools
import math
from pathlib import Path

from PIL import Image

import agave_pyvk as agave

AGAVE_PATH = "/Users/danielt/src/AllenCell/agave/build/agave.app/Contents/MacOS/agave"
DATA_URL = (
    "https://vast-files.int.allencell.org/users/alex.khang/repos/"
    "bioio_conversion/H2B_640_CAAX_488_20250904.ome.zarr"
)

OUTPUT_DIR = Path(__file__).resolve().parent / "frames"
OUTPUT_PREFIX = "frame"

FPS = 24
PIVOT_FRAMES = 2 * FPS  # 48, run twice: out and back
CLIP_IN_FRAMES = 4 * FPS  # 96
FADE_OUT_FRAMES = 2 * FPS  # 48
CLIP_OUT_FRAMES = 4 * FPS  # 96
FADE_IN_FRAMES = 2 * FPS  # 48
TOTAL_FRAMES = (
    2 * PIVOT_FRAMES
    + CLIP_IN_FRAMES
    + FADE_OUT_FRAMES
    + CLIP_OUT_FRAMES
    + FADE_IN_FRAMES
)  # 384

# Channel assignments. Source channels [2, 3] are loaded, landing as scene channels
# 0 and 1: CAAX/488 (the membrane marker) and H2B/640 (the DNA marker). The script
# prints the channel names on startup so this is easy to check.
MEMBRANE_CHANNEL = 0
DNA_CHANNEL = 1

# Channel colors, (r, g, b) in 0-1.
DNA_COLOR = (0.176043, 0.353597, 0.86276)
MEMBRANE_SPECULAR = (0.0, 0.0, 0.0)
MEMBRANE_COLOR = (0.933349, 0.271885, 0.974884)
DNA_SPECULAR = DNA_COLOR  # (0.970489, 0.0, 0.107485)

# Base opacities. The membrane channel's is what the fade animates to 0 and back.
MEMBRANE_OPACITY = 2.13
DNA_OPACITY = 4

# Scene background, (r, g, b) in 0-1, and the bounding box drawn over it.
BACKGROUND_COLOR = (0.0, 0.0, 0.0)
BOUNDING_BOX_COLOR = (1.0, 1.0, 1.0)

# renderlib's headless redraw asks for a transparent background (it passes
# backgroundAlpha = 0 to gfxApi::renderToFramebuffer), so the saved PNGs carry the
# right RGB -- the volume is already alpha-blended over BACKGROUND_COLOR -- but an
# alpha of 0 wherever no ray hit anything, which is why they look transparent.
# Dropping the alpha channel leaves exactly the intended opaque image. Set this to
# False to keep the alpha, e.g. to composite the frames over something else.
FLATTEN_ALPHA = True

# Clip plane orientation. The renderer keeps the half-space dot(normal, p) < d and
# clips away everything on the normal's side, so with +Z the cut eats down from the
# top of the stack. Flip to (0, 0, -1) to cut up from the bottom instead.
CLIP_NORMAL = (0.0, 0.0, 1.0)

# How far the plane travels, as a fraction of the displayed volume's Z thickness.
# NOTE: the scene's clip region only shows the bottom ~31% of the 245-slice stack,
# so "half the volume's Z" is measured against that displayed slab. Half of the
# *whole* 245-slice stack is thicker than the slab itself and would sweep the data
# entirely out of frame (and the membrane fade would happen on an empty image).
CLIP_TRAVEL_FRACTION = 0.5

# The opening pivot: swing this far about the camera's vertical view axis over the
# first 2 s, then unwind it over the next 2 s, with nothing else changing. Negative
# pivots the other way; 0.0 still renders those 4 s, just without any movement.
CAMERA_SWING_DEGREES = 45.0

# The original view from test.py. Each swung frame is computed from these rather
# than nudged incrementally, so 0 degrees always lands exactly back here.
CAMERA_EYE = (0.0866744, 0.5876, 0.437086)
CAMERA_TARGET = (0.463802, 0.508346, 0.16371)
CAMERA_UP = (0.490969, 0.73768, 0.46344)

# Scene constants also used to reconstruct the world-space bounding box.
VOXEL_SCALE = (0.32, 0.32, 0.5)
CLIP_REGION = (0.305882, 0.719453, 0.184314, 0.717647, 0.0, 0.311475)


def lerp(a, b, u):
    return a + (b - a) * u


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def scale(a, k):
    return tuple(x * k for x in a)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def normalize(a):
    return scale(a, 1.0 / math.sqrt(dot(a, a)))


def rotate_about_axis(v, axis, degrees):
    """Rodrigues rotation of v about a unit axis, right-hand rule."""
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    return add(
        add(scale(v, c), scale(cross(axis, v), s)),
        scale(axis, dot(axis, v) * (1.0 - c)),
    )


def vertical_view_axis():
    """The camera's up direction with any look-direction component removed."""
    forward = normalize(sub(CAMERA_TARGET, CAMERA_EYE))
    up = CAMERA_UP
    return normalize(sub(up, scale(forward, dot(up, forward))))


def set_camera_swing(r, degrees):
    """Swing the camera `degrees` about its own vertical axis, target unchanged."""
    axis = vertical_view_axis()
    offset = rotate_about_axis(sub(CAMERA_EYE, CAMERA_TARGET), axis, degrees)
    r.eye(*add(CAMERA_TARGET, offset))
    r.up(*rotate_about_axis(CAMERA_UP, axis, degrees))


def displayed_box(info):
    """World-space min/max of the part of the volume the clip region shows.

    renderlib normalizes the scene bounding box to [0,0,0] .. physical/max(physical),
    then intersects it with the clip region (expressed as 0-1 fractions).
    """
    physical = (
        info["x"] * VOXEL_SCALE[0],
        info["y"] * VOXEL_SCALE[1],
        info["z"] * VOXEL_SCALE[2],
    )
    longest = max(physical)
    extent = [p / longest for p in physical]

    box_min = tuple(CLIP_REGION[2 * i] * extent[i] for i in range(3))
    box_max = tuple(CLIP_REGION[2 * i + 1] * extent[i] for i in range(3))
    return box_min, box_max


def clip_plane_sweep(info):
    """Return (normal, d_start, d_end) describing the plane's travel.

    d_start puts the plane at the far corner of the displayed data along its normal,
    so nothing is clipped and frame 0 matches the original render. d_end is one
    travel distance back down that same axis.
    """
    length = math.sqrt(sum(c * c for c in CLIP_NORMAL))
    normal = tuple(c / length for c in CLIP_NORMAL)

    box_min, box_max = displayed_box(info)
    corners = itertools.product(*zip(box_min, box_max))
    d_start = max(sum(n * c for n, c in zip(normal, corner)) for corner in corners)

    travel = CLIP_TRAVEL_FRACTION * (box_max[2] - box_min[2])
    return normal, d_start, d_start - travel


def setup(r):
    """Everything from test.py that does not change over the animation."""
    info = r.load_data_and_get_info(
        DATA_URL, 0, 1, 0, [2, 3], [0, 1024, 0, 1024, 0, 245]
    )
    r.set_resolution(740, 762)
    r.background_color(*BACKGROUND_COLOR)
    r.show_bounding_box(1)
    r.show_scale_bar(0)
    r.show_time_stamp(0)
    r.set_time_stamp_format(1)
    r.bounding_box_color(*BOUNDING_BOX_COLOR)
    r.render_iterations(256)
    r.set_primary_ray_step_size(1)
    r.set_secondary_ray_step_size(1)
    r.set_multichannel_blend(0)
    r.set_interpolation(1)
    r.set_voxel_scale(*VOXEL_SCALE)
    r.set_flip_axis(1, 1, 1)
    r.set_clip_region(*CLIP_REGION)
    r.eye(*CAMERA_EYE)
    r.target(*CAMERA_TARGET)
    r.up(*CAMERA_UP)
    r.camera_projection(1, 0.168144)
    r.exposure(0.75)
    r.density(82.0515)
    r.aperture(0)
    r.focaldist(0.75)
    r.enable_channel(MEMBRANE_CHANNEL, 1)
    r.mat_diffuse(MEMBRANE_CHANNEL, *MEMBRANE_COLOR, 1)
    r.mat_specular(MEMBRANE_CHANNEL, *MEMBRANE_SPECULAR, 0)
    r.mat_emissive(MEMBRANE_CHANNEL, 0, 0, 0, 0)
    r.mat_glossiness(MEMBRANE_CHANNEL, 1)
    r.mat_opacity(MEMBRANE_CHANNEL, MEMBRANE_OPACITY)
    r.set_percentile_threshold(MEMBRANE_CHANNEL, 0.989187, 0.991468)
    r.set_color_ramp(MEMBRANE_CHANNEL, "none", [0, 1, 1, 1, 1, 1, 1, 1, 1, 1])
    r.enable_channel(DNA_CHANNEL, 1)
    r.mat_diffuse(DNA_CHANNEL, *DNA_COLOR, 1)
    r.mat_specular(DNA_CHANNEL, *DNA_SPECULAR, 0)
    r.mat_emissive(DNA_CHANNEL, 0, 0, 0, 0)
    r.mat_glossiness(DNA_CHANNEL, 100)
    r.mat_opacity(DNA_CHANNEL, DNA_OPACITY)
    r.set_percentile_threshold(DNA_CHANNEL, 0.991955, 0.994467)
    r.set_color_ramp(DNA_CHANNEL, "none", [0, 1, 1, 1, 1, 1, 1, 1, 1, 1])
    r.skylight_top_color(0.99028, 1, 1)
    r.skylight_middle_color(0.990311, 1, 1)
    r.skylight_bottom_color(0.990311, 1, 1)
    r.light_pos(0, 1.5, -0.425781, 1.515)
    r.light_color(0, 10, 10, 10)
    r.light_size(0, 0.15, 0.15)
    return info


def render_frame(r, frame):
    path = OUTPUT_DIR / f"{OUTPUT_PREFIX}_{frame:04d}.png"
    r.session(str(path))
    r.redraw()
    if FLATTEN_ALPHA:
        with Image.open(path) as img:
            opaque = img.convert("RGB")
        opaque.save(path)
    print(f"frame {frame + 1}/{TOTAL_FRAMES}", flush=True)


def main():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    with agave.AgaveRenderer(agave_path=AGAVE_PATH, mode="pathtrace") as r:
        info = setup(r)
        print("channels:", info.get("channel_names"))
        print(f"fading channel {MEMBRANE_CHANNEL} as the membrane channel")

        normal, d_start, d_end = clip_plane_sweep(info)
        print(f"clip plane normal {normal}, d from {d_start:.5f} to {d_end:.5f}")
        axis = vertical_view_axis()
        print(f"camera pivots {CAMERA_SWING_DEGREES} deg about {axis} and back")

        frame = 0

        # The clip plane is parked where it clips nothing for the whole camera
        # pivot, so those frames show the untouched scene from a moving view.
        r.set_clip_plane(*normal, d_start)

        # 2 s: pivot the camera out. Frame 0 is 0 degrees, i.e. the original view.
        for i in range(PIVOT_FRAMES):
            u = i / (PIVOT_FRAMES - 1)
            set_camera_swing(r, lerp(0.0, CAMERA_SWING_DEGREES, u))
            render_frame(r, frame)
            frame += 1

        # 2 s: pivot back. The last frame lands exactly on the original view, which
        # is where the camera then stays for the rest of the animation.
        for i in range(PIVOT_FRAMES):
            u = (i + 1) / PIVOT_FRAMES
            set_camera_swing(r, lerp(CAMERA_SWING_DEGREES, 0.0, u))
            render_frame(r, frame)
            frame += 1

        # 4 s: slide the plane in, camera still.
        for i in range(CLIP_IN_FRAMES):
            u = (i + 1) / CLIP_IN_FRAMES
            r.set_clip_plane(*normal, lerp(d_start, d_end, u))
            render_frame(r, frame)
            frame += 1

        # 2 s: hold the plane, fade the membrane channel to fully transparent.
        for i in range(FADE_OUT_FRAMES):
            u = (i + 1) / FADE_OUT_FRAMES
            r.mat_opacity(MEMBRANE_CHANNEL, lerp(MEMBRANE_OPACITY, 0.0, u))
            render_frame(r, frame)
            frame += 1

        # 4 s: walk the plane back to where it started.
        for i in range(CLIP_OUT_FRAMES):
            u = (i + 1) / CLIP_OUT_FRAMES
            r.set_clip_plane(*normal, lerp(d_end, d_start, u))
            render_frame(r, frame)
            frame += 1

        # 2 s: plane is parked again, bring the membrane channel back to full
        # opacity. The last frame lands back on the original scene.
        for i in range(FADE_IN_FRAMES):
            u = (i + 1) / FADE_IN_FRAMES
            r.mat_opacity(MEMBRANE_CHANNEL, lerp(0.0, MEMBRANE_OPACITY, u))
            render_frame(r, frame)
            frame += 1


if __name__ == "__main__":
    main()
