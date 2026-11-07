#!/usr/bin/env python3
"""Path-tracer throughput benchmark for the headless agave_pyvk renderer.

Reproduces the scene from an AGAVE viewer-state JSON (camera, lights,
per-channel materials and thresholds, density, step sizes) through the
Python API, then times repeated accumulation redraws at a high iteration
count. The per-iteration cost is derived as

    (t(N iterations) - t(1 iteration)) / (N - 1)

which cancels the fixed per-redraw costs (gesture overlay, tone map,
readback, PNG encode) that are identical in both measurements.

Example:
    python bench_pathtrace.py --state ~/Desktop/test.json --level 3 --iters 64
    python bench_pathtrace.py --state ~/Desktop/test.json --level 2 --woodcock both
"""

import argparse
import json
import os
import statistics
import sys
import time

import agave_pyvk


def apply_state(r, state, level, channels, width, height, zoom=1.0):
    ds = state["datasets"][0]
    load_channels = channels if channels is not None else list(ds["channels"])
    t0 = time.perf_counter()
    info = r.load_data_and_get_info(
        ds["url"], ds.get("scene", 0), level, ds.get("time", 0), load_channels, []
    )
    load_s = time.perf_counter() - t0

    r.set_resolution(width, height)
    bg = state["backgroundColor"]
    r.background_color(bg[0], bg[1], bg[2])
    r.show_bounding_box(1 if state.get("showBoundingBox") else 0)
    r.show_scale_bar(1 if state.get("showScaleBar") else 0)
    r.show_time_stamp(1 if state.get("showTimeStamp") else 0)
    bbc = state["boundingBoxColor"]
    r.bounding_box_color(bbc[0], bbc[1], bbc[2])
    pt = state["pathTracer"]
    r.set_primary_ray_step_size(pt["primaryStepSize"])
    r.set_secondary_ray_step_size(pt["secondaryStepSize"])
    if "multichannelBlendMode" in pt:
        r.set_multichannel_blend(pt["multichannelBlendMode"])
    r.set_interpolation(1 if state.get("interpolate", True) else 0)
    sc = state["scale"]
    r.set_voxel_scale(sc[0], sc[1], sc[2])
    fa = state["flipAxis"]
    r.set_flip_axis(*[(-1 if v < 0 else 1) for v in fa])
    cr = state["clipRegion"]
    r.set_clip_region(cr[0][0], cr[0][1], cr[1][0], cr[1][1], cr[2][0], cr[2][1])
    if not state["clipPlane"].get("enabled"):
        r.set_clip_plane(0, 0, 0, 0)

    cam = state["camera"]
    # zoom < 1 moves the eye toward the target so the volume fills more of the
    # frame, which makes the measurement GPU-bound rather than overhead-bound.
    eye = [t + (e - t) * zoom for e, t in zip(cam["eye"], cam["target"])]
    r.eye(*eye)
    r.target(*cam["target"])
    r.up(*cam["up"])
    if cam["projection"] == 0:
        r.camera_projection(0, cam["fovY"])
    else:
        r.camera_projection(1, cam["orthoScale"])
    r.exposure(cam["exposure"])
    r.density(state["density"])
    r.aperture(cam["aperture"])
    r.focaldist(cam["focalDistance"])

    # Channel settings are indexed by position in the loaded channel list.
    for i, src in enumerate(load_channels):
        ch = state["channels"][src]
        r.enable_channel(i, 1 if ch["enabled"] else 0)
        d = ch["diffuseColor"]
        r.mat_diffuse(i, d[0], d[1], d[2], 1.0)
        s = ch["specularColor"]
        r.mat_specular(i, s[0], s[1], s[2], 0.0)
        e = ch["emissiveColor"]
        r.mat_emissive(i, e[0], e[1], e[2], 0.0)
        r.mat_glossiness(i, ch["glossiness"])
        r.mat_opacity(i, ch["opacity"])
        lp = ch["lutParams"]
        mode = lp["mode"]
        if mode == 0:
            r.set_window_level(i, lp["window"], lp["level"])
        elif mode == 1:
            r.set_isovalue_threshold(i, lp["isovalue"], lp["isorange"])
        elif mode == 2:
            r.set_percentile_threshold(i, lp["pctLow"], lp["pctHigh"])
        elif mode == 3:
            v = []
            for p in lp["controlPoints"]:
                v.extend([p["x"], *p["value"]])
            r.set_control_points(i, v)
        elif mode == 4:
            r.set_min_max_threshold(i, lp["minu16"], lp["maxu16"])
        stops = []
        for p in ch["colorMap"]["stops"]:
            stops.extend([p["x"], *p["value"]])
        r.set_color_ramp(i, ch["colorMap"]["name"], stops)

    sky = next(l for l in state["lights"] if l["type"] == 1)
    area = next(l for l in state["lights"] if l["type"] == 0)
    r.skylight_top_color(*[c * sky["topColorIntensity"] for c in sky["topColor"]])
    r.skylight_middle_color(
        *[c * sky["middleColorIntensity"] for c in sky["middleColor"]]
    )
    r.skylight_bottom_color(
        *[c * sky["bottomColorIntensity"] for c in sky["bottomColor"]]
    )
    r.light_pos(0, area["distance"], area["theta"], area["phi"])
    r.light_color(0, *[c * area["colorIntensity"] for c in area["color"]])
    r.light_size(0, area["width"], area["height"])
    return info, load_s


def time_redraw(r, out_path, iterations):
    r.render_iterations(iterations)
    r.session(out_path)
    t0 = time.perf_counter()
    r.redraw()
    return time.perf_counter() - t0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--state", default=os.path.expanduser("~/Desktop/test.json"))
    ap.add_argument("--level", type=int, default=3, help="multiresolution level to load")
    ap.add_argument(
        "--channels",
        default="enabled",
        help="'all' loads every channel in the state; 'enabled' loads only the enabled ones (default); "
        "or a comma-separated list of channel indices",
    )
    ap.add_argument("--iters", type=int, default=64, help="iterations per timed redraw")
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--width", type=int, default=0, help="override capture width")
    ap.add_argument("--height", type=int, default=0, help="override capture height")
    ap.add_argument("--woodcock", choices=["default", "on", "off", "both"], default="both")
    ap.add_argument("--zoom", type=float, default=1.0, help="scale eye-to-target distance (<1 zooms in)")
    ap.add_argument("--out-dir", default=os.path.join(os.getcwd(), "bench_out"))
    ap.add_argument("--label", default="", help="free-form tag recorded in the JSON result line")
    ap.add_argument("--json", default="", help="append one JSON result line per mode to this file")
    args = ap.parse_args()

    with open(args.state) as f:
        state = json.load(f)
    os.makedirs(args.out_dir, exist_ok=True)

    if args.channels == "all":
        channels = None
    elif args.channels == "enabled":
        channels = [i for i, ch in enumerate(state["channels"]) if ch["enabled"]]
    else:
        channels = [int(x) for x in args.channels.split(",")]

    width = args.width or state["capture"]["width"]
    height = args.height or state["capture"]["height"]

    modes = {"default": [None], "on": [1], "off": [0], "both": [1, 0]}[args.woodcock]
    if modes != [None] and not hasattr(agave_pyvk.AgaveRenderer, "set_woodcock_tracking"):
        print("agave_pyvk has no set_woodcock_tracking; using renderer default", file=sys.stderr)
        modes = [None]

    results = []
    with agave_pyvk.AgaveRenderer(mode="pathtrace") as r:
        info, load_s = apply_state(r, state, args.level, channels, width, height, args.zoom)
        dims = (info.get("x"), info.get("y"), info.get("z"), info.get("c"))
        print(f"loaded level {args.level}: xyzc={dims} in {load_s:.1f}s; render {width}x{height}")

        for wc in modes:
            tag = {None: "default", 1: "woodcock", 0: "raymarch"}[wc]
            if wc is not None:
                r.set_woodcock_tracking(wc)
            # warm-up: volume upload, pipeline creation, first accumulation
            time_redraw(r, os.path.join(args.out_dir, f"warmup_{tag}.png"), 1)

            t1 = []
            tn = []
            for k in range(args.repeats):
                t1.append(time_redraw(r, os.path.join(args.out_dir, f"L{args.level}_z{args.zoom:g}_{tag}_1.png"), 1))
                tn.append(
                    time_redraw(r, os.path.join(args.out_dir, f"L{args.level}_z{args.zoom:g}_{tag}_{args.iters}.png"), args.iters)
                )
            med1 = statistics.median(t1)
            medn = statistics.median(tn)
            per_iter_ms = (medn - med1) / (args.iters - 1) * 1000.0
            res = {
                "label": args.label,
                "level": args.level,
                "zoom": args.zoom,
                "mode": tag,
                "width": width,
                "height": height,
                "iters": args.iters,
                "t1_ms": med1 * 1000.0,
                "tn_ms": medn * 1000.0,
                "per_iter_ms": per_iter_ms,
                "iters_per_s": 1000.0 / per_iter_ms if per_iter_ms > 0 else float("inf"),
                "t1_all_ms": [t * 1000.0 for t in t1],
                "tn_all_ms": [t * 1000.0 for t in tn],
            }
            results.append(res)
            print(
                f"[L{args.level} z{args.zoom:g} {tag:9s}] redraw(1)={med1*1000:8.1f} ms  "
                f"redraw({args.iters})={medn*1000:8.1f} ms  ->  {per_iter_ms:7.2f} ms/iter  "
                f"({res['iters_per_s']:.1f} iter/s)"
            )

    if args.json:
        with open(args.json, "a") as f:
            for res in results:
                f.write(json.dumps(res) + "\n")


if __name__ == "__main__":
    main()
