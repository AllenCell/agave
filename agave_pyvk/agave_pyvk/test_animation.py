# AGAVE Python animation (standalone, headless Vulkan)
#
# A 16 s shot declared on an animation Timeline, at 24 fps:
#
#   0- 2 s : camera pivots 45 deg, nothing else changes
#   2- 4 s : camera pivots back to the original view
#   4- 8 s : clip plane slides into the volume, camera still
#   8-10 s : membrane channel fades to 0 opacity, plane held
#  10-14 s : clip plane walks back to its original position
#  14-16 s : membrane channel fades back in, plane parked
#
# Frames 0 and 384 are both the untouched scene: the clip plane is parked at the
# far edge of the displayed data, where it clips nothing.
#
#   pip install agave_pyvk
#   python test_animation.py
#
# Then encode, e.g.:
#   ffmpeg -framerate 24 -i frames/frame_%04d.png -c:v libx264 -pix_fmt yuv420p out.mp4

import itertools
from pathlib import Path

import agave_pyvk as agave
from agave_pyvk import channels
from agave_pyvk.animation import Timeline, ease_in_out
from agave_pyvk.drivers import pivot_camera, ramp, travel_clip_plane

AGAVE_PATH = "/Users/danielt/src/AllenCell/agave/build/agave.app/Contents/MacOS/agave"
DATA_URL = (
    "https://vast-files.int.allencell.org/users/alex.khang/repos/"
    "bioio_conversion/H2B_640_CAAX_488_20250904.ome.zarr"
)

OUTPUT_DIR = Path(__file__).resolve().parent / "frames"
OUTPUT_PREFIX = "frame"

FPS = 24
PIVOT_SECS = 2
CLIP_SECS = 4
FADE_SECS = 2

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
    so nothing is clipped and the opening frame matches the untouched render. d_end
    is one travel distance back down that same axis.
    """
    length = sum(c * c for c in CLIP_NORMAL) ** 0.5
    normal = tuple(c / length for c in CLIP_NORMAL)

    box_min, box_max = displayed_box(info)
    corners = itertools.product(*zip(box_min, box_max))
    d_start = max(sum(n * c for n, c in zip(normal, corner)) for corner in corners)

    travel = CLIP_TRAVEL_FRACTION * (box_max[2] - box_min[2])
    return normal, d_start, d_start - travel


def setup(r):
    """Everything about the scene that does not change over the animation."""
    info = r.load_data_and_get_info(
        DATA_URL, 0, 1, 0, [2, 3], [0, 1024, 0, 1024, 0, 245]
    )
    r.set_resolution(740 * 2, 762 * 2)
    r.background_color(*BACKGROUND_COLOR)
    r.show_bounding_box(1)
    r.show_scale_bar(0)
    r.show_time_stamp(0)
    r.set_time_stamp_format(1)
    r.bounding_box_color(*BOUNDING_BOX_COLOR)
    r.render_iterations(512)
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


def build_timeline(normal, d_start, d_end):
    """Declare the shot. Pure: no renderer, no I/O, so it is cheap to inspect."""
    channels.alias("membrane.opacity", f"mat[{MEMBRANE_CHANNEL}].opacity")

    view = (CAMERA_EYE, CAMERA_TARGET, CAMERA_UP)
    timeline = Timeline(
        fps=FPS,
        initial={
            "camera.eye": CAMERA_EYE,
            "camera.up": CAMERA_UP,
            "clip.plane": (*normal, d_start),
            "membrane.opacity": (MEMBRANE_OPACITY,),
        },
    )
    timeline.then(
        pivot_camera(*view, 0.0, CAMERA_SWING_DEGREES, secs=PIVOT_SECS),
        pivot_camera(*view, CAMERA_SWING_DEGREES, 0.0, secs=PIVOT_SECS),
        travel_clip_plane(normal, d_start, d_end, secs=CLIP_SECS, ease=ease_in_out),
        ramp("membrane.opacity", MEMBRANE_OPACITY, 0.0, secs=FADE_SECS),
        travel_clip_plane(normal, d_end, d_start, secs=CLIP_SECS, ease=ease_in_out),
        ramp("membrane.opacity", 0.0, MEMBRANE_OPACITY, secs=FADE_SECS),
    )
    return timeline


def main():
    with agave.AgaveRenderer(agave_path=AGAVE_PATH, mode="pathtrace") as r:
        info = setup(r)
        print("channels:", info.get("channel_names"))

        normal, d_start, d_end = clip_plane_sweep(info)
        print(f"clip plane normal {normal}, d from {d_start:.5f} to {d_end:.5f}")

        timeline = build_timeline(normal, d_start, d_end)
        print(timeline.describe())

        timeline.render(
            r,
            OUTPUT_DIR,
            OUTPUT_PREFIX,
            flatten_alpha=FLATTEN_ALPHA,
            progress=lambda frame, total: print(
                f"frame {frame + 1}/{total}", flush=True
            ),
        )


if __name__ == "__main__":
    main()
