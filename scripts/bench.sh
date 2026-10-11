#!/bin/bash
# Benchmark : runs the player (ShardGame) on a world of example_project for some frames and prints how long a frame took, where
# the time went (CPU categories, GPU time of every pass, render thread), and how much memory the process holds (RAM, video
# memory, what the engine allocated on the GPU by kind). The numbers are what the player prints as "[BENCH]" lines.
#
# Usage : bash scripts/bench.sh [extra player arguments, e.g. +render.thread=0 +texture.compress=0 --view-mode unlit]
#
#   SHARD_BENCH_WORLD    the world to load (default sponza.world)
#   SHARD_BENCH_FRAMES   frames to render, the first 120 are left out of the figures (default 900)
#   SHARD_BENCH_VSYNC    1 keeps the vertical sync on (default : off, to measure the engine and not the monitor)
#   SHARD_BENCH_MIN_FPS  when set, the script fails if the average frame rate is lower (a regression guard)
#   SHARD_BENCH_MAX_VRAM_MB  when set, the script fails if the process holds more video memory than that
#
# The build/ folder is a Debug build unless it was configured with -DCMAKE_BUILD_TYPE=Release (build.bat Release) : the CPU
# figures of a Debug build are several times worse than the engine's, only the GPU ones are comparable.
set -uo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
world="${SHARD_BENCH_WORLD:-sponza.world}"
frames="${SHARD_BENCH_FRAMES:-900}"

cd "$root/build" || { echo "[ERROR] no build/ folder : build the project first." >&2; exit 1; }

player=./ShardGame
module=libGameModule.so
if [ -x ./ShardGame.exe ]; then
    player=./ShardGame.exe
    module=libGameModule.dll
fi
if [ ! -x "$player" ] || [ ! -f "$module" ]; then
    echo "[ERROR] ShardGame / $module not found in build/ : build the project first." >&2
    exit 1
fi

vsync=(--vsync)       # --vsync switches the vertical sync OFF
[ "${SHARD_BENCH_VSYNC:-0}" = "1" ] && vsync=()

log="$(mktemp)"
trap 'rm -f "$log"' EXIT

echo "[INFO] Benchmark : $world of example_project, $frames frames."
"$player" --game "$module" --project ../example_project/example_project.json --api opengl --world "$world" \
    --frames "$frames" --bench "${vsync[@]}" +render.gpu_timers=1 "$@" < /dev/null 2>&1 | tee "$log" | grep -E '^\[BENCH\]|\[ERROR\]|\[FATAL\]'
status="${PIPESTATUS[0]}"

if [ "$status" -ne 0 ]; then
    echo "[BENCH FAILED] The player exited with code $status." >&2
    exit 1
fi

failed=0

fps="$(grep -E '^\[BENCH\] frames' "$log" | sed -E 's/.*\(([0-9.]+) fps\).*/\1/')"
vram="$(grep -E '^\[BENCH\] ram' "$log" | sed -E 's/.*vram ([0-9.]+) MB.*/\1/')"
wrong="$(grep -E 'wrong thread' "$log" | sed -E 's/.*: ([0-9]+)$/\1/')"

if [ -n "${wrong:-}" ] && [ "$wrong" != "0" ]; then
    echo "[BENCH FAILED] $wrong graphics call(s) were made from a thread that does not own the graphics context." >&2
    failed=1
fi
if [ -n "${SHARD_BENCH_MIN_FPS:-}" ] && [ -n "${fps:-}" ] && awk "BEGIN{exit !($fps < $SHARD_BENCH_MIN_FPS)}"; then
    echo "[BENCH FAILED] $fps fps is under the $SHARD_BENCH_MIN_FPS fps asked for." >&2
    failed=1
fi
if [ -n "${SHARD_BENCH_MAX_VRAM_MB:-}" ] && [ -n "${vram:-}" ] && awk "BEGIN{exit !($vram > $SHARD_BENCH_MAX_VRAM_MB)}"; then
    echo "[BENCH FAILED] $vram MB of video memory is over the $SHARD_BENCH_MAX_VRAM_MB MB allowed." >&2
    failed=1
fi

[ "$failed" -ne 0 ] && exit 1
echo "[BENCH DONE] ${fps:-?} fps, ${vram:-?} MB of video memory."
