#!/usr/bin/env python3
"""Layering check for Shard (see the architecture board).

An #include from module A to module B is allowed only if B is in a strictly lower layer than A.
Includes between modules of the same layer ("peers") and upwards includes are violations.

The current violations are frozen in scripts/deps_baseline.json. The check fails when something
gets worse (new violation, higher count, new file matching a forbidden pattern) and reports when
something got better, so the baseline can be lowered with --update. It can never be raised
without --force.

  python scripts/check_deps.py            check against the baseline
  python scripts/check_deps.py --update   lower the baseline to the current state
  python scripts/check_deps.py --matrix   print every cross-module dependency
"""
import json
import os
import posixpath
import re
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(ROOT, "scripts", "deps_baseline.json")

# Lower rank = lower layer. Modules that share a rank are peers and must not include each other.
LAYERS = {
    "engine/core": 0,
    "engine/platform": 1,
    "engine/assets": 2,
    "engine/world": 3,
    "engine/renderer": 4,
    "engine/physics": 4,
    "engine/audio": 4,
    "engine/input": 4,
    "engine/root": 4.5,  # src/engine/*.cpp : the composition root (EngineInstance, game module), above every layer
    "apps/editor": 6,
    "apps/player": 6,
    "apps/game": 6,
    "apps/headless": 6,
    "apps/tools": 6,
}
GENRES_RANK = 5  # every src/genres/<name> is its own peer module

# The renderer is one module for the others, but it has layers of its own (lower rank = lower layer) :
#   rhi      - the graphics API (resources, shaders, materials, pipelines, backends)
#   scene    - what draws or lights the world with the rhi (features/, components/)
#   frontend - the Renderer that drives the frame
RENDERER_LAYERS = {
    "engine/renderer/rhi": 0,
    "engine/renderer/scene": 1,
    "engine/renderer/frontend": 2,
}

SOURCE_EXT = (".cpp", ".hpp", ".h", ".c", ".inl")
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)  # patterns are matched on code, not comments
INCLUDE = re.compile(r'#[ \t]*include[ \t]*["<]([^">\r\n]+)[">]')
INCLUDE_ROOTS = ("src/",)

# Forbidden patterns: rule name -> (regex, files it applies to, files it never applies to)
PATTERNS = {
    "gl-call-outside-backend": (
        re.compile(r"\bgl[A-Z][A-Za-z0-9]*\s*\("),
        lambda p: p.startswith("src/"),
        lambda p: p.startswith("src/engine/renderer/rhi/backends/"),
    ),
    "sdl-in-engine": (
        # the library itself (types, constants, calls, include) - not the WindowHost::SDL3 enum value
        re.compile(r'\bSDL_\w+|\bSDL[A-Z]\w*\s*\(|include\s*[<"]SDL3?/'),
        lambda p: p.startswith("src/engine/"),
        lambda p: p.startswith("src/engine/renderer/rhi/backends/") or p.startswith("src/engine/platform/"),
    ),
    "os-header-in-engine": (
        # the OS headers belong to the platform layer (PAL) : everything else asks it through its own API
        re.compile(r'include\s*[<"](windows\.h|winsock2\.h|ws2tcpip\.h|shellapi\.h|dlfcn\.h|unistd\.h|pthread\.h|sys/[a-z_]+\.h|pdh\.h|psapi\.h)[>"]'),
        lambda p: p.startswith("src/engine/"),
        lambda p: p.startswith("src/engine/renderer/rhi/backends/") or p.startswith("src/engine/platform/"),
    ),
}


def module_of(path):
    parts = path.split("/")
    if path.startswith("src/engine/renderer/rhi/backends/glad/"):
        return None  # vendored code
    if path.startswith("src/engine/renderer/") and len(parts) > 4:
        return "engine/renderer/" + {"rhi": "rhi", "features": "scene", "components": "scene"}.get(parts[3], "frontend")
    if path.startswith("src/engine/") and len(parts) == 3:
        return "engine/root"
    if path.startswith("src/engine/") and len(parts) > 3:
        return "engine/" + parts[2]
    if path.startswith("src/apps/") and len(parts) > 3:
        return "apps/" + parts[2]
    if path.startswith("src/genres/") and len(parts) > 3:
        return "genres/" + parts[2]
    return None


def group_of(module):
    return "engine/renderer" if module in RENDERER_LAYERS else module


def rank_of(module):
    if module.startswith("genres/"):
        return GENRES_RANK
    if module in RENDERER_LAYERS:
        return LAYERS["engine/renderer"]
    return LAYERS.get(module)


def is_violation(a, b):
    """True if module a may not include module b."""
    if rank_of(b) > rank_of(a):
        return True
    if rank_of(b) < rank_of(a):
        return False
    return not (a in RENDERER_LAYERS and b in RENDERER_LAYERS and RENDERER_LAYERS[b] < RENDERER_LAYERS[a])


def collect_files():
    files = {}
    for top in ("src",):
        for root, _, names in os.walk(os.path.join(ROOT, top)):
            for name in names:
                if name.endswith(SOURCE_EXT):
                    full = os.path.join(root, name)
                    files[os.path.relpath(full, ROOT).replace(os.sep, "/")] = full
    return files


def analyse():
    files = collect_files()
    edges = Counter()   # (a, b) -> number of includes
    example = {}
    unknown = set()
    patterns = {name: set() for name in PATTERNS}

    for path, full in files.items():
        text = open(full, encoding="utf-8", errors="replace").read()
        for name, (regex, applies, excluded) in PATTERNS.items():
            if applies(path) and not excluded(path) and regex.search(COMMENT.sub(" ", text)):
                patterns[name].add(path)

        a = module_of(path)
        if a is None:
            continue
        if rank_of(a) is None:
            unknown.add(a)
            continue
        directory = posixpath.dirname(path)
        for m in INCLUDE.finditer(text):
            inc = m.group(1)
            target = None
            for candidate in [posixpath.normpath(posixpath.join(directory, inc))] + [r + inc for r in INCLUDE_ROOTS]:
                if candidate in files:
                    target = candidate
                    break
            if target is None:
                continue
            b = module_of(target)
            if b is None or b == a:
                continue
            if rank_of(b) is None:
                unknown.add(b)
                continue
            edges[(a, b)] += 1
            example.setdefault((a, b), f"{path}: {inc}")
    return edges, example, unknown, {k: sorted(v) for k, v in patterns.items()}


def violations(edges):
    out = {}
    for (a, b), n in edges.items():
        if is_violation(a, b):
            kind = "peer" if rank_of(b) == rank_of(a) else "upward"
            out[f"{a} -> {b}"] = (n, kind)
    return out


def main():
    args = set(sys.argv[1:])
    edges, example, unknown, patterns = analyse()

    if unknown:
        print("Modules missing from LAYERS in scripts/check_deps.py: " + ", ".join(sorted(unknown)))
        return 1

    if "--matrix" in args:
        for (a, b), n in sorted(edges.items()):
            flag = "   <-- violation" if is_violation(a, b) else ""
            print(f"{a:26} -> {b:26} {n:4}{flag}")
        return 0

    current = violations(edges)
    state = {
        "edges": {k: n for k, (n, _) in sorted(current.items())},
        "patterns": patterns,
    }
    total_inc = sum(state["edges"].values())
    total_files = sum(len(v) for v in patterns.values())

    baseline = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {"edges": {}, "patterns": {}}

    worse, better = [], []
    for key, n in state["edges"].items():
        old = baseline["edges"].get(key, 0)
        if n > old:
            a, b = (tuple(key.split(" -> ")))
            worse.append(f"  {key} ({current[key][1]}): {old} -> {n}   e.g. {example[(a, b)]}")
    for key, old in baseline["edges"].items():
        n = state["edges"].get(key, 0)
        if n < old:
            better.append(f"  {key}: {old} -> {n}")
    for name, files in patterns.items():
        old = set(baseline["patterns"].get(name, []))
        for f in files:
            if f not in old:
                worse.append(f"  [{name}] new offender: {f}")
        for f in old - set(files):
            better.append(f"  [{name}] fixed: {f}")

    if "--update" in args:
        if worse and os.path.exists(BASELINE) and "--force" not in args:
            print("Refusing to raise the baseline (use --force if this is really intended):")
            print("\n".join(worse))
            return 1
        with open(BASELINE, "w", newline="\n") as fh:
            json.dump(state, fh, indent=2, sort_keys=True)
            fh.write("\n")
        print(f"Baseline written: {len(state['edges'])} violating module pairs ({total_inc} includes), "
              f"{total_files} pattern offenders.")
        return 0

    print(f"Layering: {len(state['edges'])} violating module pairs ({total_inc} includes), "
          f"{total_files} forbidden-pattern files.")
    if better:
        print("Improved since the baseline (run with --update to lock it in):")
        print("\n".join(better))
    if worse:
        print("REGRESSIONS:")
        print("\n".join(worse))
        return 1
    print("OK: nothing worse than the baseline.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
