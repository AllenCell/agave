#!/usr/bin/env python3
"""Benchmark Vulkan path tracing with a reproducible four-channel volume.

The volume contains four overlapping, smooth spheres in a black field. Use
``--shape 256 1024 2048 --data gaussian-noise`` for a production-size volume:
Gaussian background noise is clipped to [0, 63], and sphere signals occupy
[160, 255]. Volumes over 512 MiB use a temporary memory-mapped file.
``--mode matrix`` covers high
and low density and opacity, Max and Weighted blending, Woodcock tracking, and
fixed-step ray marching at default and fine primary/secondary step sizes.
``--mode grid`` isolates the Woodcock majorant grid; run it twice, setting
``AGAVE_PT_DISABLE_MAJORANT_GRID=1`` for the second process.
``--mode steps`` varies primary and secondary steps independently.

Each case times headless redraws with one and N iterations, then reports
``(median(tN) - median(t1)) / (N - 1)``. This estimate cancels most fixed
readback and Python overhead, but it is not a GPU timestamp. Small per-sample
times can be noisy; compare repeated runs and use a Release build. The first
redraw includes volume upload and pipeline setup and is reported separately.

Example:
    python bench_synthetic_vulkan.py --mode matrix --json run.jsonl
"""

import argparse
import json
import os
import platform
import statistics
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path

import agave_py
import numpy as np
from PIL import Image


@dataclass(frozen=True)
class Case:
    name: str
    woodcock: int
    blend: int
    primary_step: float
    secondary_step: float
    density: float
    opacity: float


def fill_volume(data: np.ndarray, kind: str, seed: int, slab_depth: int) -> int:
    """Fill CZYX uint8 data in Z slabs and return the signal voxel count."""
    _, size_z, size_y, size_x = data.shape
    axis_z = (np.arange(size_z, dtype=np.float32) + 0.5) / size_z - 0.5
    axis_y = (np.arange(size_y, dtype=np.float32) + 0.5) / size_y - 0.5
    axis_x = (np.arange(size_x, dtype=np.float32) + 0.5) / size_x - 0.5
    rng = np.random.default_rng(seed)
    spheres = (
        (-0.15, -0.10, 0.00, 0.32),
        (0.17, 0.10, 0.05, 0.26),
        (0.00, -0.16, 0.15, 0.22),
        (-0.05, 0.16, -0.13, 0.20),
    )
    signal_voxels = 0
    for channel, (cx, cy, cz, radius) in enumerate(spheres):
        xy_distance = (axis_y[:, None] - cy) ** 2 + (axis_x[None, :] - cx) ** 2
        for start in range(0, size_z, slab_depth):
            stop = min(start + slab_depth, size_z)
            slab = data[channel, start:stop]
            if kind == "gaussian-noise":
                noise = rng.standard_normal(slab.shape, dtype=np.float32)
                noise *= 8.0
                noise += 24.0
                np.clip(noise, 0, 63, out=noise)
                slab[:] = noise.astype(np.uint8)
            else:
                slab.fill(0)

            z_distance = (axis_z[start:stop] - cz)[:, None, None] ** 2
            signal = 1.0 - (xy_distance[None, :, :] + z_distance) / radius**2
            mask = signal > 0
            signal_voxels += int(np.count_nonzero(mask))
            if kind == "gaussian-noise":
                signal *= 95.0
                signal += 160.0
            else:
                signal *= 255.0
            slab[mask] = np.clip(signal[mask], 0, 255).astype(np.uint8)
    return signal_voxels


def cases_for_mode(mode: str) -> list[Case]:
    if mode == "smoke":
        return [Case("smoke", 1, 0, 4.0, 4.0, 8.5, 1.0)]
    if mode == "grid":
        return [
            Case("grid_sparse", 1, 0, 0.5, 0.5, 1.0, 0.15),
            Case("grid_dense", 1, 0, 0.5, 0.5, 20.0, 1.0),
        ]
    if mode == "woodcock":
        return [
            Case("sparse_woodcock_max", 1, 0, 4.0, 4.0, 1.0, 0.15),
            Case("sparse_woodcock_weighted", 1, 1, 4.0, 4.0, 1.0, 0.15),
            Case("dense_woodcock_max", 1, 0, 4.0, 4.0, 20.0, 1.0),
            Case("dense_woodcock_weighted", 1, 1, 4.0, 4.0, 20.0, 1.0),
        ]
    if mode == "steps":
        return [
            Case(
                f"steps_primary{primary:g}_secondary{secondary:g}",
                0,
                0,
                primary,
                secondary,
                1.0,
                0.15,
            )
            for primary, secondary in ((4.0, 4.0), (0.5, 4.0), (4.0, 0.5), (0.5, 0.5))
        ]
    if mode == "representative":
        return [
            Case("sparse_woodcock_max", 1, 0, 0.5, 0.5, 1.0, 0.15),
            Case("sparse_woodcock_weighted", 1, 1, 0.5, 0.5, 1.0, 0.15),
            Case("sparse_raymarch_max", 0, 0, 0.5, 0.5, 1.0, 0.15),
            Case("sparse_raymarch_weighted", 0, 1, 0.5, 0.5, 1.0, 0.15),
            Case("dense_woodcock_max", 1, 0, 0.5, 0.5, 20.0, 1.0),
            Case("dense_woodcock_weighted", 1, 1, 0.5, 0.5, 20.0, 1.0),
            Case("dense_raymarch_max", 0, 0, 0.5, 0.5, 20.0, 1.0),
            Case("dense_raymarch_weighted", 0, 1, 0.5, 0.5, 20.0, 1.0),
            Case("sparse_raymarch_primary4_secondary05", 0, 0, 4.0, 0.5, 1.0, 0.15),
            Case("sparse_raymarch_primary05_secondary4", 0, 0, 0.5, 4.0, 1.0, 0.15),
        ]

    cases = []
    for density, opacity in ((1.0, 0.15), (1.0, 1.0), (20.0, 0.15), (20.0, 1.0)):
        for blend, blend_name in ((0, "max"), (1, "weighted")):
            suffix = f"{blend_name}_density{density:g}_opacity{opacity:g}"
            cases.append(
                Case(f"woodcock_{suffix}", 1, blend, 4.0, 4.0, density, opacity)
            )
            for step in (4.0, 0.5):
                cases.append(
                    Case(
                        f"raymarch_step{step:g}_{suffix}",
                        0,
                        blend,
                        step,
                        step,
                        density,
                        opacity,
                    )
                )
    return cases


def redraw_pixels(renderer, iterations: int):
    """Time native redraw and readback without PNG encoding in the timed part."""
    renderer.render_iterations(iterations)
    start = time.perf_counter()
    image = renderer._execute("REDRAW")
    return (time.perf_counter() - start) * 1000.0, image


def image_stats(image) -> dict:
    width, height, pixels = image
    bgra = np.frombuffer(pixels, dtype=np.uint8).reshape(height, width, 4)
    bgr = bgra[:, :, :3]
    return {
        "rgb_mean": round(float(bgr.mean()), 4),
        "rgb_std": round(float(bgr.std()), 4),
        "nonblack_fraction": round(float(np.any(bgr > 8, axis=2).mean()), 5),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--size", type=int, default=256, help="volume side in voxels")
    parser.add_argument(
        "--shape",
        type=int,
        nargs=3,
        metavar=("Z", "Y", "X"),
        help="volume ZYX shape; overrides --size",
    )
    parser.add_argument(
        "--data", choices=("spheres", "gaussian-noise"), default="spheres"
    )
    parser.add_argument("--seed", type=int, default=12345)
    parser.add_argument("--slab-depth", type=int, default=4)
    parser.add_argument("--memmap", action="store_true", help="force temporary memmap")
    parser.add_argument("--scratch-dir", type=Path, help="temporary volume directory")
    parser.add_argument("--width", type=int, default=512)
    parser.add_argument("--height", type=int, default=512)
    parser.add_argument("--iterations", type=int, default=64)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument(
        "--mode",
        choices=("smoke", "matrix", "grid", "woodcock", "steps", "representative"),
        default="matrix",
    )
    parser.add_argument("--label", default="", help="build or run label")
    parser.add_argument("--json", type=Path, help="append JSON lines to this file")
    parser.add_argument("--images", type=Path, help="save the final PNG for each case")
    args = parser.parse_args()
    shape = tuple(args.shape or (args.size, args.size, args.size))
    if min(*shape, args.width, args.height) < 2:
        parser.error("volume dimensions, width, and height must be at least 2")
    if args.iterations < 2 or args.repeats < 1:
        parser.error("iterations must be at least 2 and repeats at least 1")
    if args.slab_depth < 1:
        parser.error("slab-depth must be at least 1")
    if args.images:
        args.images.mkdir(parents=True, exist_ok=True)

    def emit(record: dict) -> None:
        line = json.dumps(record, sort_keys=True)
        print(line, flush=True)
        if args.json:
            with args.json.open("a", encoding="utf-8") as stream:
                stream.write(line + "\n")

    volume_bytes = 4 * shape[0] * shape[1] * shape[2]
    use_memmap = args.memmap or volume_bytes >= 512 * 1024**2
    backing_file = None
    if use_memmap:
        backing_file = tempfile.NamedTemporaryFile(
            prefix="agave-bench-", suffix=".u8", dir=args.scratch_dir, delete=True
        )
        data = np.memmap(backing_file, dtype=np.uint8, mode="w+", shape=(4, *shape))
    else:
        data = np.empty((4, *shape), dtype=np.uint8)

    try:
        start = time.perf_counter()
        signal_voxels = fill_volume(data, args.data, args.seed, args.slab_depth)
        generate_ms = (time.perf_counter() - start) * 1000.0
        native = Path(agave_py.agave._native.__file__).resolve()
        emit(
            {
                "record": "run",
                "label": args.label,
                "mode": args.mode,
                "platform": platform.platform(),
                "native": str(native),
                "native_bytes": native.stat().st_size,
                "majorant_grid_disabled": os.getenv("AGAVE_PT_DISABLE_MAJORANT_GRID")
                == "1",
                "data_kind": args.data,
                "seed": args.seed,
                "noise_distribution": "normal(mean=24, std=8), clipped to [0, 63]"
                if args.data == "gaussian-noise"
                else None,
                "signal_range": [160, 255]
                if args.data == "gaussian-noise"
                else [0, 255],
                "signal_voxels": signal_voxels,
                "signal_fraction": round(signal_voxels / data.size, 5),
                "memmap": use_memmap,
                "volume_shape_czyx": list(data.shape),
                "volume_bytes": data.nbytes,
                "converted_uint16_cpu_bytes": 2 * data.nbytes,
                "rgba16_gpu_texture_bytes": 2 * data.nbytes,
                "resolution": [args.width, args.height],
                "iterations": args.iterations,
                "repeats": args.repeats,
                "generate_ms": round(generate_ms, 2),
            }
        )

        with agave_py.AgaveRenderer(mode="pathtrace") as renderer:
            start = time.perf_counter()
            info = renderer.load_array(
                data,
                name="synthetic_spheres_with_noise"
                if args.data == "gaussian-noise"
                else "synthetic_spheres",
                channel_names=["red", "green", "blue", "yellow"],
            )
            load_ms = (time.perf_counter() - start) * 1000.0
            renderer.set_resolution(args.width, args.height)
            renderer.show_bounding_box(0)
            renderer.show_scale_bar(0)
            renderer.show_time_stamp(0)
            renderer.background_color(0.0, 0.0, 0.0)
            renderer.skylight_top_color(1.0, 1.0, 1.0)
            renderer.skylight_middle_color(0.7, 0.7, 0.7)
            renderer.skylight_bottom_color(0.4, 0.4, 0.4)
            colors = (
                (1.0, 0.05, 0.05),
                (0.05, 1.0, 0.05),
                (0.05, 0.2, 1.0),
                (1.0, 0.8, 0.2),
            )
            for channel, color in enumerate(colors):
                renderer.enable_channel(channel, 1)
                renderer.mat_diffuse(channel, *color, 1.0)
                renderer.set_min_max_threshold(
                    channel, 160 if args.data == "gaussian-noise" else 0, 255
                )
            emit(
                {
                    "record": "load_array",
                    "label": args.label,
                    "load_ms": round(load_ms, 2),
                    "dimensions": [info["x"], info["y"], info["z"], info["c"]],
                }
            )

            for case in cases_for_mode(args.mode):
                renderer.set_woodcock_tracking(case.woodcock)
                renderer.set_multichannel_blend(case.blend)
                renderer.set_primary_ray_step_size(case.primary_step)
                renderer.set_secondary_ray_step_size(case.secondary_step)
                renderer.density(case.density)
                for channel in range(4):
                    renderer.mat_opacity(channel, case.opacity)

                warmup_ms, _ = redraw_pixels(renderer, 1)
                one_ms = []
                many_ms = []
                image = None
                for _ in range(args.repeats):
                    elapsed, _ = redraw_pixels(renderer, 1)
                    one_ms.append(elapsed)
                    elapsed, image = redraw_pixels(renderer, args.iterations)
                    many_ms.append(elapsed)
                per_iteration_ms = (
                    statistics.median(many_ms) - statistics.median(one_ms)
                ) / (args.iterations - 1)
                emit(
                    {
                        "record": "case",
                        "label": args.label,
                        **asdict(case),
                        "warmup_ms": round(warmup_ms, 2),
                        "one_ms": [round(value, 2) for value in one_ms],
                        "many_ms": [round(value, 2) for value in many_ms],
                        "per_iteration_ms": round(per_iteration_ms, 4),
                        **image_stats(image),
                    }
                )
                if args.images:
                    width, height, pixels = image
                    Image.frombytes(
                        "RGBA", (width, height), pixels, "raw", "BGRA"
                    ).save(args.images / f"{case.name}.png")

    finally:
        if backing_file is not None:
            data._mmap.close()
            backing_file.close()


if __name__ == "__main__":
    main()
