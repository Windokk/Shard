#!/usr/bin/env python3
"""Compares pictures. Three commands :

  compare_frames.py diff A B        how two pictures (.png, or linear .hdr) differ : mean / RMSE / PSNR, percentiles, the pixels off
                                    by more than N levels, the areas that differ most, and a map of the difference
  compare_frames.py apis            renders a world with OpenGL, then with Vulkan, from the same views, and runs diff on each pair
  compare_frames.py reference       the real-time renderer against the path tracer, one term of the lighting at a time (below)

diff and apis exit with 1 when the pictures break --max-mean / --max-p99 / --max-over, so they can gate a CI step.

reference : compares the real-time renderer against the path tracer, one term of the lighting at a time.

The player renders a world from one or several views (render.camera), and for each view :
  - the path tracer renders it (raytrace.render ... terms), which writes the picture and its light split by term ;
  - the real-time renderer draws it once per term (render.lighting_terms) and writes each frame in linear light
    (render.screenshot <file>.hdr).
The terms (src/engine/renderer/rhi/lighting_terms.hpp) : direct (the lights), indirect_diffuse (sky, bounces, emitters, ambient
floor : the probes, lightmaps, SSAO), indirect_specular (reflections : environment map, reflection probes, SSR), emission (the
surfaces' own light and the sky seen directly).

The report says, for all the views together and for each one, how much of the light each term carries, whether the real-time term
is too bright or too dark overall (in stops), and how far it is from the reference area by area. It is written in --out
(report.html, report.json, and per view one PNG per term : reference | real time | where it is too bright (red) or too dark (blue)).
The views of a world are in scripts/reference_views.json (the world's camera when it is not listed), or given with --view.

Standard library only. Run from anywhere : python scripts/compare_frames.py reference --world sponza.world
"""

import argparse
import json
import math
import os
import re
import struct
import subprocess
import sys
import time
import zlib
from array import array

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VIEWS_FILE = os.path.join(ROOT, "scripts", "reference_views.json")
TERMS = ["direct", "indirect_diffuse", "indirect_specular", "emission"]
LUMA = (0.2126, 0.7152, 0.0722)


def read_hdr(path):
    """(width, height, [r, g, b] arrays of floats, top row first)"""
    with open(path, "rb") as f:
        data = f.read()

    pos = 0
    size_line = None
    while pos < len(data):
        end = data.index(b"\n", pos)
        line = data[pos:end].strip()
        pos = end + 1
        if line.startswith(b"-Y") or line.startswith(b"+Y"):
            size_line = line
            break
    match = re.match(rb"([-+])Y (\d+) \+X (\d+)", size_line or b"")
    if not match or match.group(1) != b"-":
        raise ValueError(f"{path} : unsupported .hdr layout ({size_line!r})")
    height, width = int(match.group(2)), int(match.group(3))

    scale = [math.ldexp(1.0, e - 136) if e else 0.0 for e in range(256)]
    channels = [array("f", bytes(4 * width * height)) for _ in range(3)]
    r_out, g_out, b_out = channels

    for y in range(height):
        if width >= 8 and width < 32768 and data[pos] == 2 and data[pos + 1] == 2 and data[pos + 2] < 128:
            pos += 4
            planes = []
            for _ in range(4):
                plane = bytearray()
                while len(plane) < width:
                    count = data[pos]
                    pos += 1
                    if count > 128:
                        plane += bytes((data[pos],)) * (count - 128)
                        pos += 1
                    else:
                        plane += data[pos:pos + count]
                        pos += count
                planes.append(plane)
            r_in, g_in, b_in, e_in = planes
        else:
            row = data[pos:pos + 4 * width]
            pos += 4 * width
            r_in, g_in, b_in, e_in = row[0::4], row[1::4], row[2::4], row[3::4]

        base = y * width
        for x in range(width):
            s = scale[e_in[x]]
            r_out[base + x] = r_in[x] * s
            g_out[base + x] = g_in[x] * s
            b_out[base + x] = b_in[x] * s

    return width, height, channels


def write_png(path, width, height, rgb):
    """`rgb` : bytes / bytearray of width * height * 3, top row first"""
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        f.write(chunk(b"IEND", b""))


def srgb_byte(value):
    value = min(max(value, 0.0), 1.0)
    encoded = value * 12.92 if value <= 0.0031308 else 1.055 * value ** (1.0 / 2.4) - 0.055
    return int(encoded * 255.0 + 0.5)


def luminance(channels):
    r, g, b = channels
    return array("f", (LUMA[0] * r[i] + LUMA[1] * g[i] + LUMA[2] * b[i] for i in range(len(r))))


def block_means(values, width, height, block):
    """The mean of every block x block square (the edge that does not fill a block is left out), top row first"""
    bw, bh = width // block, height // block
    sums = [0.0] * (bw * bh)
    for y in range(bh * block):
        row = y * width
        out = (y // block) * bw
        for bx in range(bw):
            start = row + bx * block
            sums[out + bx] += sum(values[start:start + block])
    area = float(block * block)
    return [s / area for s in sums], bw, bh


def percentile(values, fraction):
    ordered = sorted(values)
    if not ordered:
        return 0.0
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))]


def stops(ratio):
    return math.log2(ratio) if ratio > 0.0 else float("-inf")


def measure(ref, raster, width, height, block):
    """The sums one term of one view adds to the metrics : they add up over the views (see accumulate, finish). Also what its
    picture needs (the blocks, the reference's luminance)."""
    ref_y, raster_y = luminance(ref), luminance(raster)

    ref_blocks, bw, bh = block_means(ref_y, width, height, block)
    raster_blocks, _, _ = block_means(raster_y, width, height, block)
    block_ref = sum(ref_blocks)

    # Block error in stops. The real-time block is floored at 1% of the term's mean light, so a term it never draws gives log2(reference / floor), not infinity.
    floor = 0.01 * (block_ref / max(len(ref_blocks), 1))

    sums = {
        "ref": sum(ref_y),
        "raster": sum(raster_y),
        "ref_rgb": [sum(c) for c in ref],
        "raster_rgb": [sum(c) for c in raster],
        "block_ref": block_ref,
        "brighter": sum(max(q - p, 0.0) for p, q in zip(ref_blocks, raster_blocks)),
        "darker": sum(max(p - q, 0.0) for p, q in zip(ref_blocks, raster_blocks)),
        "pixel_error": sum(abs(p - q) for p, q in zip(ref_y, raster_y)),
        "log_errors": [abs(math.log2(max(q, floor) / p)) for p, q in zip(ref_blocks, raster_blocks) if p > floor],
    }
    return sums, {"blocks": (ref_blocks, raster_blocks, bw, bh), "ref_y": ref_y}


def accumulate(total, sums):
    if total is None:
        return {k: (list(v) if isinstance(v, list) else v) for k, v in sums.items()}
    for key, value in sums.items():
        if key == "log_errors":
            total[key] += value
        elif isinstance(value, list):
            total[key] = [a + b for a, b in zip(total[key], value)]
        else:
            total[key] += value
    return total


def finish(sums, image):
    """The metrics of a term from its sums, `image` being the sums of the whole picture ("all") over the same views"""
    def ratio(a, b):
        return a / b if b > 0.0 else None

    channel_bias = [round(stops(b / a), 3) if a > 0.0 and b > 0.0 else None for a, b in zip(sums["ref_rgb"], sums["raster_rgb"])]
    errors = sums["log_errors"]
    return {
        "share": ratio(sums["ref"], image["ref"]),
        "bias_stops": stops(sums["raster"] / sums["ref"]) if sums["ref"] > 0.0 and sums["raster"] > 0.0 else None,
        "channel_bias_stops": channel_bias,
        "block_error": ratio(sums["brighter"] + sums["darker"], sums["block_ref"]),
        "too_bright": ratio(sums["brighter"], sums["block_ref"]),
        "too_dark": ratio(sums["darker"], sums["block_ref"]),
        "pixel_error": ratio(sums["pixel_error"], sums["ref"]),
        "image_error": ratio(sums["brighter"] + sums["darker"], image["block_ref"]),
        "median_stops": percentile(errors, 0.5) if errors else None,
        "p90_stops": percentile(errors, 0.9) if errors else None,
        "ref_energy": sums["ref"],
        "raster_energy": sums["raster"],
    }


def diverging(t):
    """-1 : blue (too dark), 0 : light grey, +1 : red (too bright)"""
    t = min(max(t, -1.0), 1.0)
    mid = (221, 221, 221)
    end = (180, 4, 38) if t > 0.0 else (59, 76, 192)
    a = abs(t)
    return tuple(int(mid[i] + (end[i] - mid[i]) * a) for i in range(3))


def write_strip(path, ref, raster, extra, width, height, block, range_stops):
    """reference | real time | error map, side by side, exposed alike (the 99th percentile of the reference is white)"""
    white = percentile([v for v in extra["ref_y"] if v > 0.0], 0.99) or 1.0
    exposure = 1.0 / white

    gap = 4
    total = width * 3 + gap * 2
    pixels = bytearray(bytes((24, 24, 24)) * (total * height))

    for panel, channels in ((0, ref), (1, raster)):
        r, g, b = channels
        x0 = panel * (width + gap)
        for y in range(height):
            out = (y * total + x0) * 3
            src = y * width
            row = bytearray(width * 3)
            for x in range(width):
                row[x * 3] = srgb_byte(r[src + x] * exposure)
                row[x * 3 + 1] = srgb_byte(g[src + x] * exposure)
                row[x * 3 + 2] = srgb_byte(b[src + x] * exposure)
            pixels[out:out + width * 3] = row

    ref_blocks, raster_blocks, bw, bh = extra["blocks"]
    count = max(len(ref_blocks), 1)
    floor = 0.01 * (sum(ref_blocks) / count) or 0.01 * (sum(raster_blocks) / count) or 1e-12
    colours = []
    for p, q in zip(ref_blocks, raster_blocks):
        if max(p, q) <= floor:
            colours.append(bytes((40, 40, 40)))
        else:
            colours.append(bytes(diverging(math.log2(max(q, floor) / max(p, floor)) / range_stops)))
    x0 = 2 * (width + gap)
    for y in range(min(height, bh * block)):
        out = (y * total + x0) * 3
        by = y // block
        row = bytearray()
        for bx in range(bw):
            row += colours[by * bw + bx] * block
        pixels[out:out + len(row)] = row

    write_png(path, total, height, pixels)


def parse_view(text):
    """"name=px py pz tx ty tz [fov]", "name=camera" or "camera" (the world's own camera)"""
    name, _, view = text.partition("=")
    if not view:
        name, view = (text, "") if text == "camera" else ("view", text)
    view = view.strip()
    if view in ("", "camera"):
        return {"name": name.strip() or "camera", "view": None}
    numbers = view.replace(",", " ").split()
    if len(numbers) not in (6, 7):
        sys.exit(f"[ERROR] a view is 'name=px py pz tx ty tz [fov]' : {text!r}")
    try:
        [float(n) for n in numbers]
    except ValueError:
        sys.exit(f"[ERROR] a view is 'name=px py pz tx ty tz [fov]' : {text!r}")
    return {"name": name.strip(), "view": " ".join(numbers)}


def views_of(args):
    if args.view:
        views = [parse_view(v) for v in args.view]
    else:
        listed = {}
        if os.path.isfile(VIEWS_FILE):
            with open(VIEWS_FILE, encoding="utf-8") as f:
                listed = json.load(f)
        entries = listed.get(os.path.basename(args.world)) or [{"name": "camera"}]
        views = [{"name": e["name"], "view": e.get("view")} for e in entries]

    names = [v["name"] for v in views]
    if len(set(names)) != len(names) or any(not re.match(r"^[A-Za-z0-9_.-]+$", n) for n in names):
        sys.exit(f"[ERROR] the views need distinct names made of letters, digits, '_', '-', '.' : {names}")
    return views


def reference_files():
    return ["reference.hdr"] + [f"reference.{term}.hdr" for term in TERMS]


def kept_reference(folder, args, view):
    """The settings of the reference already in `folder` when it was made for this world, size and view, else None"""
    try:
        with open(os.path.join(folder, "reference.json"), encoding="utf-8") as f:
            settings = json.load(f)
    except (OSError, ValueError):
        return None
    if not all(os.path.isfile(os.path.join(folder, name)) for name in reference_files()):
        return None
    if (settings.get("world"), settings.get("width"), settings.get("height"), settings.get("view")) != (args.world, args.width, args.height, view["view"]):
        return None
    return settings


def find_player(build):
    for name in ("ShardGame.exe", "ShardGame"):
        candidate = os.path.join(build, name)
        if os.path.isfile(candidate):
            return candidate
    return None


def write_commands(path, out, args, views, render_reference):
    """The console script the player runs : "<frame> <command>" lines (see EngineCreationSettings::scriptFile)"""
    settle = 8          # render thread lags the simulation
    view_settle = 30
    lines = ["0 timescale 0"]
    if args.pipeline:
        lines.append(f"0 render.pipeline {args.pipeline}")
    for command in args.command:
        lines.append(f"0 {command}")

    frame = args.warmup
    for view, render in zip(views, render_reference):
        folder = f"{out}/{view['name']}"
        lines.append(f"{frame} render.camera {view['view'] or 'off'}")
        lines.append(f"{frame} render.lighting_terms all")
        frame += view_settle
        lines.append(f'{frame} render.screenshot "{folder}/raster.all.hdr"')
        if render:
            frame += 1
            lines.append(f'{frame} raytrace.render "{folder}/reference.hdr" {args.width} {args.height} {args.spp} {args.bounces} terms')
        for term in TERMS:
            frame += 1
            lines.append(f"{frame} render.lighting_terms {term}")
            frame += settle
            lines.append(f'{frame} render.screenshot "{folder}/raster.{term}.hdr"')
        frame += 1
        lines.append(f"{frame} render.lighting_terms all")
        frame += 1
    lines.append(f"{frame} render.camera off")

    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return frame + 3


def run_player(args, out, views, render_reference):
    build = os.path.abspath(args.build)
    player = find_player(build)
    if not player:
        sys.exit(f"[ERROR] no ShardGame in {build} : build the project first (cmake --build build --target ShardGame)")
    if " " in out:
        sys.exit(f"[ERROR] the output folder cannot contain spaces (console paths) : {out}")

    commands = os.path.join(out, "commands.txt")
    frames = write_commands(commands, out, args, views, render_reference)

    env = dict(os.environ)
    mingw = r"C:\msys64\mingw64\bin"
    if os.name == "nt" and os.path.isdir(mingw):
        env["PATH"] = mingw + os.pathsep + env.get("PATH", "")

    cmd = [player, "--project", os.path.abspath(args.project), "--api", args.api, "--world", args.world,
           "--frames", str(frames), "--width", str(args.width), "--height", str(args.height), "--vsync", "0",
           "--script", commands]
    traced = sum(1 for r in render_reference if r)
    print(f"[INFO] {args.world} : {len(views)} view(s), {args.warmup} warm-up frames, "
          + (f"path tracer {args.width}x{args.height} at {args.spp} spp and {args.bounces} bounces for {traced} of them" if traced else "references kept"))

    log_path = os.path.join(out, "player.log")
    failures = []
    start = time.time()
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        process = subprocess.Popen(cmd, cwd=build, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL, text=True, encoding="utf-8", errors="replace")
        for line in process.stdout:
            log.write(line)
            if "Script @" in line:
                if args.verbose or " FAILED" in line or "raytrace.render" in line or "render.camera" in line:
                    print("  " + line.strip()[:220])
                if " FAILED" in line:
                    failures.append(line.strip())
            elif "[ERROR]" in line or "[FATAL]" in line:
                print("  " + line.strip()[:220])
            if time.time() - start > args.timeout:
                process.kill()
                sys.exit(f"[ERROR] no result after {args.timeout} s (--timeout) : see {log_path}")
        status = process.wait()

    if status != 0:
        sys.exit(f"[ERROR] the player exited with {status} : see {log_path}")
    if failures:
        sys.exit(f"[ERROR] {len(failures)} command(s) failed : see {log_path}")


def fmt_pct(value):
    return "-" if value is None else f"{value * 100:.1f}%"


def fmt_stops(value):
    return "-" if value is None else f"{value:+.2f}"


def print_table(metrics):
    header = f"{'term':<18} {'share':>7} {'bias':>7} {'error':>7} {'bright':>7} {'dark':>7} {'median':>7} {'p90':>7} {'pixel':>7} {'of image':>9}"
    print(header)
    print("-" * len(header))
    for term in ["all"] + TERMS:
        m = metrics[term]
        print(f"{term:<18} {fmt_pct(m['share']):>7} {fmt_stops(m['bias_stops']):>7} {fmt_pct(m['block_error']):>7} "
              f"{fmt_pct(m['too_bright']):>7} {fmt_pct(m['too_dark']):>7} {fmt_stops(m['median_stops']):>7} "
              f"{fmt_stops(m['p90_stops']):>7} {fmt_pct(m['pixel_error']):>7} {fmt_pct(m['image_error']):>9}")


def reference_main(argv):
    parser = argparse.ArgumentParser(prog="compare_frames.py reference", description="Compares the real-time renderer against the path tracer, term by term.")
    parser.add_argument("--world", default="sponza.world", help="world of the project to render")
    parser.add_argument("--view", action="append", default=[],
                        help="a view, 'name=px py pz tx ty tz [fov]' (position, point looked at, vertical fov) or 'camera' (the world's) ; "
                             "repeatable ; default : the world's views in scripts/reference_views.json, else its camera")
    parser.add_argument("--project", default=os.path.join(ROOT, "example_project", "example_project.json"))
    parser.add_argument("--build", default=os.path.join(ROOT, "build"), help="build folder holding ShardGame")
    parser.add_argument("--api", default="opengl", choices=["opengl", "vulkan"])
    parser.add_argument("--pipeline", default=None, help="render pipeline to compare (render.pipeline : deferred, forward_plus...) ; the project's by default")
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=360)
    parser.add_argument("--spp", type=int, default=256, help="samples per pixel of the reference")
    parser.add_argument("--bounces", type=int, default=6)
    parser.add_argument("--warmup", type=int, default=900, help="frames before the first capture : live GI probes need ~800 in a Debug build")
    parser.add_argument("--block", type=int, default=8, help="size in pixels of the areas compared (they average the reference's noise away)")
    parser.add_argument("--range", type=float, default=2.0, help="stops at which the error map saturates")
    parser.add_argument("--timeout", type=int, default=3600, help="seconds the player may take")
    parser.add_argument("--out", default=None, help="output folder (default : build/reference_compare/<world>)")
    parser.add_argument("--reuse", action="store_true", help="do not run the player : compare the pictures already in --out")
    parser.add_argument("--keep-reference", action="store_true",
                        help="render only the real-time side of the views whose reference is already in --out (to iterate on the real-time renderer)")
    parser.add_argument("--command", action="append", default=[],
                        help="console command run at the start, before the captures (repeatable), e.g. --command \"render.msaa off\"")
    parser.add_argument("--verbose", action="store_true", help="print every console command the player runs")
    args = parser.parse_args(argv)

    world_name = os.path.splitext(os.path.basename(args.world))[0]
    out = os.path.abspath(args.out or os.path.join(args.build, "reference_compare", world_name)).replace("\\", "/")
    views = views_of(args)
    for view in views:
        os.makedirs(os.path.join(out, view["name"]), exist_ok=True)

    references = [kept_reference(os.path.join(out, v["name"]), args, v) if (args.keep_reference or args.reuse) else None for v in views]
    if not args.reuse:
        run_player(args, out, views, [r is None for r in references])
        for i, view in enumerate(views):
            if references[i] is None:
                references[i] = {"world": args.world, "width": args.width, "height": args.height, "view": view["view"],
                                 "spp": args.spp, "bounces": args.bounces}
                with open(os.path.join(out, view["name"], "reference.json"), "w", encoding="utf-8", newline="\n") as f:
                    json.dump(references[i], f, indent=2)

    combined = {}
    per_view = []
    for view, reference in zip(views, references):
        folder = os.path.join(out, view["name"])
        pairs = [("all", "reference.hdr", "raster.all.hdr")] + [(t, f"reference.{t}.hdr", f"raster.{t}.hdr") for t in TERMS]
        missing = [name for _, a, b in pairs for name in (a, b) if not os.path.isfile(os.path.join(folder, name))]
        if missing:
            sys.exit(f"[ERROR] missing in {folder} : {', '.join(missing)}")

        sums = {}
        for term, ref_name, raster_name in pairs:
            rw, rh, ref = read_hdr(os.path.join(folder, ref_name))
            sw, sh, raster = read_hdr(os.path.join(folder, raster_name))
            if (rw, rh) != (sw, sh):
                sys.exit(f"[ERROR] {ref_name} is {rw}x{rh} but {raster_name} is {sw}x{sh} : the window did not get the size asked for")
            sums[term], extra = measure(ref, raster, rw, rh, args.block)
            write_strip(os.path.join(folder, f"compare.{term}.png"), ref, raster, extra, rw, rh, args.block, args.range)
            combined[term] = accumulate(combined.get(term), sums[term])

        metrics = {term: finish(sums[term], sums["all"]) for term in ["all"] + TERMS}
        terms_sum = {
            "reference": sum(sums[t]["ref"] for t in TERMS) / max(sums["all"]["ref"], 1e-12),
            "raster": sum(sums[t]["raster"] for t in TERMS) / max(sums["all"]["raster"], 1e-12),
        }
        per_view.append({"name": view["name"], "view": view["view"], "spp": (reference or {}).get("spp"),
                         "bounces": (reference or {}).get("bounces"), "terms_sum": terms_sum, "terms": metrics})
        print(f"[INFO] compared {view['name']}")

    total = {term: finish(combined[term], combined["all"]) for term in ["all"] + TERMS}
    report = {
        "world": args.world, "api": args.api, "pipeline": args.pipeline or "project default",
        "width": args.width, "height": args.height, "block": args.block, "commands": args.command,
        "combined": total, "views": per_view,
    }
    with open(os.path.join(out, "report.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(report, f, indent=2)

    print()
    print(f"All {len(views)} view(s) :")
    print_table(total)
    if len(views) > 1:
        print()
        header = f"{'view':<14} {'bias':>7} {'error':>7} " + " ".join(f"{t[:17]:>17}" for t in TERMS) + "   (of image)"
        print(header)
        print("-" * len(header))
        for v in per_view:
            m = v["terms"]
            print(f"{v['name']:<14} {fmt_stops(m['all']['bias_stops']):>7} {fmt_pct(m['all']['block_error']):>7} "
                  + " ".join(f"{fmt_pct(m[t]['image_error']):>17}" for t in TERMS))
    print()
    print("share : the part of the reference's light in the term ; bias : real time against reference, overall, in stops ;")
    print(f"error : |real time - reference| per {args.block}x{args.block} area, over the term's light (bright / dark : the parts above / below) ;")
    print("median / p90 : stops off, over the areas the term lights ; pixel : the same error pixel by pixel ; of image : the error over the whole picture's light.")
    worst = max(abs(v["terms_sum"][side] - 1.0) for v in per_view for side in ("reference", "raster"))
    print(f"terms add up to the picture within {worst * 100:.1f}% in every view")

    write_html(os.path.join(out, "report.html"), report, args)
    print(f"\n[INFO] report : {out}/report.html")


def table_html(metrics):
    rows = []
    for term in ["all"] + TERMS:
        m = metrics[term]
        rows.append(
            f"<tr><td>{term}</td><td>{fmt_pct(m['share'])}</td><td>{fmt_stops(m['bias_stops'])}</td>"
            f"<td>{' / '.join('-' if b is None else f'{b:+.2f}' for b in m['channel_bias_stops'])}</td>"
            f"<td>{fmt_pct(m['block_error'])}</td><td>{fmt_pct(m['too_bright'])}</td><td>{fmt_pct(m['too_dark'])}</td>"
            f"<td>{fmt_stops(m['median_stops'])}</td><td>{fmt_stops(m['p90_stops'])}</td>"
            f"<td>{fmt_pct(m['pixel_error'])}</td><td>{fmt_pct(m['image_error'])}</td></tr>")
    return ("<table><tr><th>term</th><th>share</th><th>bias (stops)</th><th>R / G / B bias</th><th>error</th><th>too bright</th>"
            "<th>too dark</th><th>median (stops)</th><th>p90 (stops)</th><th>pixel error</th><th>of image</th></tr>"
            + "".join(rows) + "</table>")


def write_html(path, report, args):
    sections = []
    for v in report["views"]:
        where = "the world's camera" if v["view"] is None else f"from {' '.join(v['view'].split()[:3])} toward {' '.join(v['view'].split()[3:6])}"
        images = "\n".join(f'<h3>{term}</h3><img src="{v["name"]}/compare.{term}.png" alt="{v["name"]}, {term} : reference, real time, error map">'
                           for term in ["all"] + TERMS)
        sections.append(f"<h2>{v['name']}</h2><p>{where} ; reference at {v['spp']} spp and {v['bounces']} bounces ; "
                        f"the terms add up to the picture : reference {v['terms_sum']['reference'] * 100:.1f}%, "
                        f"real time {v['terms_sum']['raster'] * 100:.1f}%.</p>{table_html(v['terms'])}{images}")

    commands = "".join(f", <code>{c}</code>" for c in report["commands"])
    html = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Reference comparison</title>
<style>
:root {{ --bg: #ffffff; --fg: #1d1d1f; --muted: #6e6e73; --line: #d2d2d7; }}
@media (prefers-color-scheme: dark) {{ :root {{ --bg: #161617; --fg: #f5f5f7; --muted: #a1a1a6; --line: #3a3a3c; }} }}
body {{ background: var(--bg); color: var(--fg); font: 14px/1.5 system-ui, sans-serif; margin: 24px auto; max-width: 1980px; padding: 0 16px; }}
table {{ border-collapse: collapse; font-variant-numeric: tabular-nums; }}
th, td {{ border-bottom: 1px solid var(--line); padding: 4px 10px; text-align: right; }}
th:first-child, td:first-child {{ text-align: left; }}
p, li {{ color: var(--muted); max-width: 900px; }}
img {{ max-width: 100%; image-rendering: pixelated; }}
</style></head><body>
<h1>{report['world']} : real time against the path tracer</h1>
<p>{report['api']}, pipeline {report['pipeline']}{commands}, {report['width']}x{report['height']}, areas of {report['block']}x{report['block']} px, {len(report['views'])} view(s).</p>
<h2>All views</h2>
{table_html(report['combined'])}
<ul>
<li><b>share</b> : the part of the reference picture's light in the term.</li>
<li><b>bias</b> : the real-time term's light against the reference's, over the whole picture, in stops (+1 : twice too bright). R / G / B : the same per channel, a difference between them is a tint.</li>
<li><b>error</b> : the sum of |real time - reference| over areas of {report['block']}x{report['block']} px, over the term's light ; <b>too bright / too dark</b> : the areas above / below the reference.</li>
<li><b>median / p90</b> : how many stops the areas the term lights are off. <b>pixel error</b> : the error pixel by pixel (adds edges, aliasing, fine shadows and the reference's noise).</li>
<li><b>of image</b> : the term's error over the whole picture's light : the terms that matter most for the picture come first.</li>
</ul>
<p>Each picture : reference | real time (both exposed so the reference's 99th percentile is white) | error map per area,
log2(real time / reference) : red too bright, blue too dark, saturated at &plusmn;{args.range:g} stops, dark grey where neither has light.</p>
{''.join(sections)}
</body></html>
"""
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(html)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def read_png(path):
    """(width, height, rgb bytearray of width * height * 3, top row first). 8 bits per channel, not interlaced (what the engine writes)"""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"[ERROR] not a PNG : {path}")

    pos, header, palette, idat = 8, None, b"", bytearray()
    while pos + 8 <= len(data):
        size, kind = struct.unpack(">I4s", data[pos:pos + 8])
        payload = data[pos + 8:pos + 8 + size]
        pos += 12 + size
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", payload)
        elif kind == b"PLTE":
            palette = payload
        elif kind == b"IDAT":
            idat += payload
        elif kind == b"IEND":
            break
    if header is None:
        sys.exit(f"[ERROR] PNG without header : {path}")
    width, height, depth, colour, _, _, interlace = header
    if depth != 8 or interlace != 0 or colour not in (0, 2, 3, 4, 6):
        sys.exit(f"[ERROR] only 8-bit, non-interlaced PNGs are read ({depth} bits, colour type {colour}, interlace {interlace}) : {path}")

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colour]
    stride = width * channels
    raw = zlib.decompress(bytes(idat))
    rows, previous = [], bytearray(stride)
    pos = 0
    for _ in range(height):
        kind = raw[pos]
        row = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        if kind == 1:
            for i in range(channels, stride):
                row[i] = (row[i] + row[i - channels]) & 255
        elif kind == 2:
            for i in range(stride):
                row[i] = (row[i] + previous[i]) & 255
        elif kind == 3:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                row[i] = (row[i] + (left + previous[i]) // 2) & 255
        elif kind == 4:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                corner = previous[i - channels] if i >= channels else 0
                row[i] = (row[i] + paeth(left, previous[i], corner)) & 255
        rows.append(row)
        previous = row

    rgb = bytearray(width * height * 3)
    for y, row in enumerate(rows):
        out = y * width * 3
        if colour == 2:
            rgb[out:out + width * 3] = row
        elif colour == 6:
            rgb[out:out + width * 3:3] = row[0::4]
            rgb[out + 1:out + width * 3:3] = row[1::4]
            rgb[out + 2:out + width * 3:3] = row[2::4]
        elif colour in (0, 4):
            grey = row[0::channels]
            for c in range(3):
                rgb[out + c:out + width * 3:3] = grey
        else:
            for x in range(width):
                index = row[x] * 3
                rgb[out + x * 3:out + x * 3 + 3] = palette[index:index + 3]
    return width, height, rgb


def read_picture(path):
    """(width, height, rgb bytes) of a .png, or of a linear .hdr as it would be shown (sRGB, no exposure)"""
    if path.lower().endswith(".hdr"):
        width, height, (r, g, b) = read_hdr(path)
        rgb = bytearray(width * height * 3)
        rgb[0::3] = bytes(srgb_byte(v) for v in r)
        rgb[1::3] = bytes(srgb_byte(v) for v in g)
        rgb[2::3] = bytes(srgb_byte(v) for v in b)
        return width, height, rgb
    return read_png(path)


def heat(t):
    """0 : black, 1 : white, through blue, red and yellow"""
    t = min(max(t, 0.0), 1.0)
    stops_ = ((0.0, (0, 0, 0)), (0.25, (40, 40, 200)), (0.55, (220, 30, 30)), (0.8, (255, 210, 40)), (1.0, (255, 255, 255)))
    for (t0, c0), (t1, c1) in zip(stops_, stops_[1:]):
        if t <= t1:
            k = (t - t0) / (t1 - t0)
            return bytes(int(c0[i] + (c1[i] - c0[i]) * k) for i in range(3))
    return bytes(stops_[-1][1])


LEVELS = (1, 2, 4, 8, 16, 32)


def diff_pictures(a, b, width, height, block, gain_levels):
    """Everything the report says about two pictures of the same size, and the picture of where they differ.
    The error of a pixel is its largest channel difference, in levels of 0..255."""
    count = width * height
    error = bytearray(count)
    sum_abs = [0, 0, 0]
    sum_sq = [0, 0, 0]
    sum_signed = [0, 0, 0]
    for c in range(3):
        ac, bc = a[c::3], b[c::3]
        total_abs = total_sq = total_signed = 0
        for i, (p, q) in enumerate(zip(ac, bc)):
            d = q - p
            total_signed += d
            if d < 0:
                d = -d
            total_abs += d
            total_sq += d * d
            if d > error[i]:
                error[i] = d
        sum_abs[c], sum_sq[c], sum_signed[c] = total_abs, total_sq, total_signed

    histogram = [0] * 256
    for e in error:
        histogram[e] += 1

    def percentile_of(fraction):
        target = fraction * count
        seen = 0
        for level, n in enumerate(histogram):
            seen += n
            if seen >= target:
                return level
        return 255

    mse = sum(sum_sq) / (3.0 * count)
    metrics = {
        "width": width, "height": height,
        "identical": max(error) == 0,
        "mean_abs": sum(sum_abs) / (3.0 * count),
        "mean_abs_rgb": [s / count for s in sum_abs],
        "bias_rgb": [s / count for s in sum_signed],
        "rmse": math.sqrt(mse),
        "psnr": float("inf") if mse == 0 else 10.0 * math.log10(255.0 * 255.0 / mse),
        "max": max(error),
        "median": percentile_of(0.5), "p90": percentile_of(0.9), "p99": percentile_of(0.99), "p999": percentile_of(0.999),
        "over": {str(level): sum(histogram[level + 1:]) / count for level in LEVELS},
    }

    # Where : the block x block areas, by mean error
    bw, bh = width // block, height // block
    areas = []
    for by in range(bh):
        for bx in range(bw):
            total = 0
            for y in range(by * block, (by + 1) * block):
                start = y * width + bx * block
                total += sum(error[start:start + block])
            areas.append((total / float(block * block), bx * block, by * block))
    areas.sort(reverse=True)
    metrics["worst_areas"] = [{"x": x, "y": y, "size": block, "mean_abs": round(m, 3)} for m, x, y in areas[:5] if m > 0]

    # The picture : A | B | where they differ
    gap = 4
    total_w = width * 3 + gap * 2
    pixels = bytearray(bytes((24, 24, 24)) * (total_w * height))
    palette = [heat(level / gain_levels) for level in range(256)]
    for y in range(height):
        out = y * total_w * 3
        src = y * width * 3
        pixels[out:out + width * 3] = a[src:src + width * 3]
        pixels[out + (width + gap) * 3:out + (width + gap) * 3 + width * 3] = b[src:src + width * 3]
        row = bytearray()
        for e in error[y * width:(y + 1) * width]:
            row += palette[e]
        pixels[out + 2 * (width + gap) * 3:out + 2 * (width + gap) * 3 + width * 3] = row
    return metrics, total_w, pixels


def print_diff(metrics, name_a, name_b, gain_levels):
    m = metrics
    print(f"{name_a}  vs  {name_b}   ({m['width']}x{m['height']})")
    if m["identical"]:
        print("  identical, pixel for pixel")
        return
    psnr = "inf" if m["psnr"] == float("inf") else f"{m['psnr']:.1f} dB"
    print(f"  mean error {m['mean_abs']:.3f} / 255   (R {m['mean_abs_rgb'][0]:.3f}  G {m['mean_abs_rgb'][1]:.3f}  B {m['mean_abs_rgb'][2]:.3f})")
    print(f"  bias       R {m['bias_rgb'][0]:+.3f}  G {m['bias_rgb'][1]:+.3f}  B {m['bias_rgb'][2]:+.3f}   ({name_b} minus {name_a}, + : {name_b} is brighter)")
    print(f"  RMSE {m['rmse']:.3f}   PSNR {psnr}   worst pixel {m['max']}")
    print(f"  pixel error  median {m['median']}   p90 {m['p90']}   p99 {m['p99']}   p99.9 {m['p999']}")
    print("  pixels off by more than " + "   ".join(f"{level}: {m['over'][str(level)] * 100:.2f}%" for level in LEVELS))
    if m["worst_areas"]:
        print("  worst areas (x, y, mean error) : " + "   ".join(f"{w['x']},{w['y']} {w['mean_abs']:.1f}" for w in m["worst_areas"]))
    print(f"  (map : black = same, white = {gain_levels}+ levels off)")


def verdict(metrics, args):
    """The reasons the comparison fails the limits given (--max-mean, --max-p99, --max-over), empty when it passes"""
    reasons = []
    if args.max_mean is not None and metrics["mean_abs"] > args.max_mean:
        reasons.append(f"mean error {metrics['mean_abs']:.3f} > {args.max_mean}")
    if args.max_p99 is not None and metrics["p99"] > args.max_p99:
        reasons.append(f"p99 pixel error {metrics['p99']} > {args.max_p99}")
    if args.max_over is not None and metrics["over"]["8"] * 100.0 > args.max_over:
        reasons.append(f"{metrics['over']['8'] * 100:.2f}% of the pixels are off by more than 8 > {args.max_over}%")
    return reasons


def add_diff_options(parser):
    parser.add_argument("--block", type=int, default=16, help="size in pixels of the areas that rank where the pictures differ most")
    parser.add_argument("--gain", type=int, default=32, help="levels (of 255) at which the difference map is white")
    parser.add_argument("--max-mean", type=float, default=None, help="fail (exit 1) when the mean error, in levels of 255, is above this")
    parser.add_argument("--max-p99", type=int, default=None, help="fail when the 99th percentile of the pixel error is above this")
    parser.add_argument("--max-over", type=float, default=None, help="fail when more than this percentage of the pixels are off by more than 8 levels")


def compare_two(path_a, path_b, name_a, name_b, out, args, stem="diff"):
    """Compares two picture files, prints the report, writes <out>/<stem>.png and <stem>.json. Returns (metrics, failures)."""
    wa, ha, a = read_picture(path_a)
    wb, hb, b = read_picture(path_b)
    if (wa, ha) != (wb, hb):
        sys.exit(f"[ERROR] {name_a} is {wa}x{ha} but {name_b} is {wb}x{hb} : the pictures must be the same size")
    metrics, total_w, pixels = diff_pictures(a, b, wa, ha, max(args.block, 1), max(args.gain, 1))
    metrics.update({"a": path_a, "b": path_b})
    failures = verdict(metrics, args)
    metrics["failures"] = failures

    os.makedirs(out, exist_ok=True)
    write_png(os.path.join(out, stem + ".png"), total_w, ha, pixels)
    with open(os.path.join(out, stem + ".json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(metrics, f, indent=2)
    print_diff(metrics, name_a, name_b, max(args.gain, 1))
    return metrics, failures


def diff_main(argv):
    parser = argparse.ArgumentParser(prog="compare_frames.py diff", description="Reports how two pictures of the same size differ (.png, or linear .hdr).")
    parser.add_argument("a", help="first picture (the reference)")
    parser.add_argument("b", help="second picture")
    parser.add_argument("--out", default=None, help="output folder (default : next to the second picture)")
    add_diff_options(parser)
    args = parser.parse_args(argv)

    out = os.path.abspath(args.out or os.path.dirname(os.path.abspath(args.b)))
    stem = f"diff.{os.path.splitext(os.path.basename(args.a))[0]}.{os.path.splitext(os.path.basename(args.b))[0]}"
    _, failures = compare_two(args.a, args.b, os.path.basename(args.a), os.path.basename(args.b), out, args, stem)
    print(f"\n[INFO] {os.path.join(out, stem)}.png (A | B | difference) and .json")
    if failures:
        sys.exit("[FAIL] " + " ; ".join(failures))


def write_api_commands(path, folder, args, views):
    """The console script of one API's run : one PNG per view"""
    settle = 8          # the render thread lags the simulation
    lines = ["0 timescale 0"]
    if args.pipeline:
        lines.append(f"0 render.pipeline {args.pipeline}")
    for command in args.command:
        lines.append(f"0 {command}")
    frame = args.warmup
    for view in views:
        lines.append(f"{frame} render.camera {view['view'] or 'off'}")
        frame += settle
        lines.append(f'{frame} render.screenshot "{folder}/{view["name"]}.png"')
        frame += 1
    lines.append(f"{frame} render.camera off")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return frame + 3


def run_api(args, out, api, views):
    """Runs the player on one API, with the same script for every API : <out>/<api>/<view>.png"""
    build = os.path.abspath(args.build)
    player = find_player(build)
    if not player:
        sys.exit(f"[ERROR] no ShardGame in {build} : build the project first (cmake --build build --target ShardGame)")
    folder = f"{out}/{api}"
    os.makedirs(folder, exist_ok=True)
    for view in views:
        stale = os.path.join(folder, view["name"] + ".png")
        if os.path.isfile(stale):
            os.remove(stale)

    commands = os.path.join(folder, "commands.txt")
    frames = write_api_commands(commands, folder, args, views)
    env = dict(os.environ)
    mingw = r"C:\msys64\mingw64\bin"
    if os.name == "nt" and os.path.isdir(mingw):
        env["PATH"] = mingw + os.pathsep + env.get("PATH", "")
    if api == "vulkan":
        env.setdefault("SHARD_VULKAN_VALIDATION", "0")

    cmd = [player, "--project", os.path.abspath(args.project), "--api", api, "--world", args.world,
           "--frames", str(frames), "--width", str(args.width), "--height", str(args.height), "--vsync", "0",
           "--script", commands, "--seed", str(args.seed)]
    print(f"[INFO] {api} : {args.world}, {len(views)} view(s), {args.warmup} warm-up frames")

    log_path = os.path.join(folder, "player.log")
    failures = []
    start = time.time()
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        process = subprocess.Popen(cmd, cwd=build, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL, text=True, encoding="utf-8", errors="replace")
        for line in process.stdout:
            log.write(line)
            if "Script @" in line:
                if args.verbose or " FAILED" in line:
                    print("  " + line.strip()[:220])
                if " FAILED" in line:
                    failures.append(line.strip())
            elif "[ERROR]" in line or "[FATAL]" in line:
                print("  " + line.strip()[:220])
            if time.time() - start > args.timeout:
                process.kill()
                sys.exit(f"[ERROR] no result after {args.timeout} s (--timeout) : see {log_path}")
        status = process.wait()
    if status != 0:
        sys.exit(f"[ERROR] the {api} player exited with {status} : see {log_path}")
    if failures:
        sys.exit(f"[ERROR] {len(failures)} command(s) failed on {api} : see {log_path}")
    for view in views:
        if not os.path.isfile(os.path.join(folder, view["name"] + ".png")):
            sys.exit(f"[ERROR] {api} wrote no picture for the view {view['name']} : see {log_path}")


def apis_main(argv):
    parser = argparse.ArgumentParser(prog="compare_frames.py apis",
                                     description="Renders a world with OpenGL and with Vulkan, from the same views, and reports how the pictures differ.")
    parser.add_argument("--world", default="sponza.world", help="world of the project to render")
    parser.add_argument("--view", action="append", default=[],
                        help="a view, 'name=px py pz tx ty tz [fov]' or 'camera' (the world's) ; repeatable ; "
                             "default : the world's views in scripts/reference_views.json, else its camera")
    parser.add_argument("--project", default=os.path.join(ROOT, "example_project", "example_project.json"))
    parser.add_argument("--build", default=os.path.join(ROOT, "build"), help="build folder holding ShardGame")
    parser.add_argument("--pipeline", default=None, help="render pipeline (render.pipeline : deferred, forward_plus...) ; the project's by default")
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=360)
    parser.add_argument("--warmup", type=int, default=60, help="frames before the captures (the live GI probes need ~800 in a Debug build)")
    parser.add_argument("--seed", type=int, default=7, help="simulation seed, the same on both APIs")
    parser.add_argument("--timeout", type=int, default=1800, help="seconds each player may take")
    parser.add_argument("--out", default=None, help="output folder (default : build/api_compare/<world>)")
    parser.add_argument("--reuse", action="store_true", help="do not run the players : compare the pictures already in --out")
    parser.add_argument("--command", action="append", default=[], help="console command run at the start on both APIs (repeatable)")
    parser.add_argument("--verbose", action="store_true", help="print every console command the players run")
    add_diff_options(parser)
    args = parser.parse_args(argv)

    world_name = os.path.splitext(os.path.basename(args.world))[0]
    out = os.path.abspath(args.out or os.path.join(args.build, "api_compare", world_name)).replace("\\", "/")
    if " " in out:
        sys.exit(f"[ERROR] the output folder cannot contain spaces (console paths) : {out}")
    views = views_of(args)

    if not args.reuse:
        for api in ("opengl", "vulkan"):
            run_api(args, out, api, views)

    results = []
    failed = []
    for view in views:
        print()
        metrics, failures = compare_two(f"{out}/opengl/{view['name']}.png", f"{out}/vulkan/{view['name']}.png",
                                        f"opengl/{view['name']}", f"vulkan/{view['name']}", out, args, f"diff.{view['name']}")
        results.append({"name": view["name"], "view": view["view"], **metrics})
        failed += [f"{view['name']} : {reason}" for reason in failures]

    report = {"world": args.world, "pipeline": args.pipeline or "project default", "width": args.width, "height": args.height,
              "warmup": args.warmup, "views": results, "failures": failed}
    with open(os.path.join(out, "report.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(report, f, indent=2)

    if len(results) > 1:
        print()
        print(f"{'view':<14} {'mean':>8} {'rmse':>8} {'p99':>5} {'max':>5} {'>8 levels':>10}")
        for r in results:
            print(f"{r['name']:<14} {r['mean_abs']:>8.3f} {r['rmse']:>8.3f} {r['p99']:>5} {r['max']:>5} {r['over']['8'] * 100:>9.2f}%")
    print(f"\n[INFO] pictures, difference maps (diff.<view>.png : opengl | vulkan | difference) and report.json : {out}")
    if failed:
        sys.exit("[FAIL] " + " ; ".join(failed))


COMMANDS = {"diff": diff_main, "apis": apis_main, "reference": reference_main}


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in COMMANDS:
        print("usage: compare_frames.py {diff A B | apis | reference} [options]   (--help after a command for its options)\n"
              "  diff       how two pictures differ\n"
              "  apis       the same world rendered with OpenGL and with Vulkan, compared\n"
              "  reference  the real-time renderer against the path tracer, term by term")
        sys.exit(0 if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help") else 2)
    COMMANDS[sys.argv[1]](sys.argv[2:])


if __name__ == "__main__":
    main()
