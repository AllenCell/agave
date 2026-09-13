"""Load an OME-TIFF two ways and check that both render the same frame.

The first pass hands AGAVE a file path and lets renderlib's own reader do the
loading (``load_data``). The second pass reads the file with bioio and pushes
the resulting NumPy array straight into the in-process headless Vulkan renderer
(``load_array``), so nothing is read from disk by AGAVE at all. Both passes use
identical scene settings, each writes a PNG, and the two images are compared.

The array path works for any volume you can get into a ZYX or CZYX array, not
just files AGAVE knows how to read: segmentations, simulation output, etc.

    pip install bioio bioio-ome-tiff
    python -m agave_pyvk.example4 mydata.ome.tiff out
"""

import sys

import numpy
from bioio import BioImage
from PIL import Image

from agave_pyvk import AgaveRenderer

# AGAVE's volume format is uint16; anything else gets converted on the way in.
SUPPORTED_DTYPES = (numpy.uint8, numpy.uint16, numpy.float32)

# a few distinct colors to cycle through for the first channels
CHANNEL_COLORS = [
    (1.0, 0.0, 1.0),
    (1.0, 1.0, 1.0),
    (0.0, 1.0, 1.0),
    (1.0, 1.0, 0.0),
]


def read_volume(filename, scene=0, timepoint=0):
    """Return (CZYX array, (x,y,z) voxel size, spatial units, channel names)."""
    img = BioImage(filename)
    img.set_scene(scene)

    data = img.get_image_data("CZYX", T=timepoint)
    if data.dtype not in [numpy.dtype(d) for d in SUPPORTED_DTYPES]:
        # uint16 keeps full precision for the integer types AGAVE cares about
        data = data.astype(numpy.uint16)

    # bioio reports pixel sizes as (Z, Y, X); any of them can be None
    sizes = img.physical_pixel_sizes
    voxel_size = (
        sizes.X or 1.0,
        sizes.Y or 1.0,
        sizes.Z or 1.0,
    )

    channel_names = [str(name) for name in img.channel_names]
    return data, voxel_size, "micron", channel_names


def render(r, info, output_name):
    """Apply identical scene settings to whatever volume is loaded, and render."""
    r.set_resolution(1024, 768)
    r.background_color(0, 0, 0)
    r.show_bounding_box(1)
    r.bounding_box_color(1, 1, 1)
    r.show_scale_bar(0)
    r.render_iterations(64)
    r.set_primary_ray_step_size(4)
    r.set_secondary_ray_step_size(4)
    r.set_interpolation(1)
    r.set_clip_region(0, 1, 0, 1, 0, 1)

    # loading already sized the scene; frame_scene points the camera at it
    r.camera_projection(0, 55)
    r.frame_scene()
    r.exposure(0.75)
    r.density(50)
    r.aperture(0)
    r.focaldist(0.75)

    # show the first few channels, autoscaled by intensity percentile
    for channel in range(info["c"]):
        enabled = channel < len(CHANNEL_COLORS)
        r.enable_channel(channel, 1 if enabled else 0)
        if not enabled:
            continue
        red, green, blue = CHANNEL_COLORS[channel]
        r.mat_diffuse(channel, red, green, blue, 1)
        r.mat_specular(channel, 0, 0, 0, 0)
        r.mat_emissive(channel, 0, 0, 0, 0)
        r.mat_glossiness(channel, 1)
        r.mat_opacity(channel, 1)
        r.set_percentile_threshold(channel, 0.5, 0.98)

    r.skylight_top_color(1, 1, 1)
    r.skylight_middle_color(1, 1, 1)
    r.skylight_bottom_color(1, 1, 1)
    r.light_pos(0, 1.5, 5.54884, 0.952752)
    r.light_color(0, 10, 10, 10)
    r.light_size(0, 0.15, 0.15)

    # session() names the file that the next redraw() writes
    r.session(output_name)
    r.redraw()
    print(f"wrote {output_name}")


def describe_volume(label, info):
    # both loaders return the same structure, so this works for either
    voxel = (info["pixel_size_x"], info["pixel_size_y"], info["pixel_size_z"])
    print(
        f"{label}: {info['c']}x{info['z']}x{info['y']}x{info['x']} (czyx)"
        f" voxel {voxel} {info['spatial_units']}"
        f" channels {info['channel_names']}"
        f" intensity {info['channel_min_intensity']}..{info['channel_max_intensity']}"
    )


def compare_images(path_a, path_b):
    """Report how far apart the two rendered frames are."""
    a = numpy.asarray(Image.open(path_a).convert("RGBA"), dtype=numpy.int16)
    b = numpy.asarray(Image.open(path_b).convert("RGBA"), dtype=numpy.int16)
    if a.shape != b.shape:
        print(f"images differ in size: {a.shape} vs {b.shape}")
        return

    difference = numpy.abs(a - b)
    differing = int(numpy.count_nonzero(difference.any(axis=2)))
    total = a.shape[0] * a.shape[1]
    print(
        f"comparison: {differing}/{total} pixels differ"
        f" ({100.0 * differing / total:.3f}%),"
        f" max channel delta {int(difference.max())},"
        f" mean {difference.mean():.4f}"
    )
    if differing == 0:
        print("the two load paths produced identical images")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    filename = sys.argv[1]
    prefix = sys.argv[2] if len(sys.argv) > 2 else "example4"
    file_output = f"{prefix}_load_data.png"
    array_output = f"{prefix}_load_array.png"

    data, voxel_size, spatial_units, channel_names = read_volume(filename)
    print(f"bioio read {filename}: shape {data.shape} {data.dtype}")

    # One renderer for both passes, so the only thing that varies is how the
    # volume got in. Each load resets the scene bounds, and render() reapplies
    # every setting afterwards.
    with AgaveRenderer(mode="pathtrace") as r:
        file_info = r.load_data(filename, 0, 0, 0, [], [])
        describe_volume("renderlib load_data", file_info)
        render(r, file_info, file_output)

        array_info = r.load_array(
            data,
            name=filename,
            voxel_size=voxel_size,
            spatial_units=spatial_units,
            channel_names=channel_names,
        )
        describe_volume("bioio load_array ", array_info)
        render(r, array_info, array_output)

    compare_images(file_output, array_output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
