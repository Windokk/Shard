#!/bin/bash
# Stress test : runs the player on example_project through a script of console commands (see scripts/stress_script.txt :
# framebuffer resizes, play mode in and out, world loads, multisampling toggles, thumbnails), once with the render
# thread and once without, and fails when a run crashes, hangs, logs an [ERROR] / [FATAL], or makes a graphics call from the
# wrong thread. The render thread is the point : it must behave like drawing on the main thread, only faster.
#
# Usage : bash scripts/stress_test.sh      (after a build : it uses build/ShardGame and build/plugins/game/bin/*/libGameModule)
#
#   SHARD_STRESS_FRAMES   frames per run (default 1200 : the script runs until frame 900)
#   SHARD_STRESS_TIMEOUT  seconds before a run is given up as hung (default 300)
set -uo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
frames="${SHARD_STRESS_FRAMES:-1200}"
timeout_seconds="${SHARD_STRESS_TIMEOUT:-300}"
script="$root/scripts/stress_script.txt"

# Errors that say something about the machine, not about the engine : the CI has no sound card.
ignored_errors='ma_engine_init failed'

cd "$root/build" || { echo "[ERROR] no build/ folder : build the project first." >&2; exit 1; }

player=./ShardGame
module=$(ls plugins/game/bin/*/libGameModule.so 2>/dev/null | head -n 1)
module=${module:-plugins/game/bin/*/libGameModule.so}
if [ -x ./ShardGame.exe ]; then
    player=./ShardGame.exe
    module=$(ls plugins/game/bin/*/libGameModule.dll 2>/dev/null | head -n 1)
    module=${module:-plugins/game/bin/*/libGameModule.dll}
fi
if [ ! -x "$player" ] || [ ! -f "$module" ]; then
    echo "[ERROR] ShardGame / $module not found in build/ : build the project first." >&2
    exit 1
fi

log="$(mktemp)"
trap 'rm -f "$log"' EXIT

failed=0
for mode in 1 0; do
    echo "[INFO] Stress test : render.thread=$mode, $frames frames."

    timeout "$timeout_seconds" "$player" --game "$module" --project ../example_project/example_project.json --api opengl \
        --world cornell.world --frames "$frames" --width 640 --height 360 --vsync --script "$script" \
        +render.thread=$mode < /dev/null > "$log" 2>&1
    status=$?

    if [ "$status" -eq 124 ]; then
        echo "[STRESS TEST FAILED] render.thread=$mode : no result after ${timeout_seconds}s (hang or deadlock)." >&2
        failed=1
    elif [ "$status" -ne 0 ]; then
        echo "[STRESS TEST FAILED] render.thread=$mode : the player exited with code $status." >&2
        tail -20 "$log" >&2
        failed=1
    elif ! grep -q "shut down cleanly" "$log"; then
        echo "[STRESS TEST FAILED] render.thread=$mode : the player exited without going through its shutdown." >&2
        failed=1
    fi

    errors="$(grep -E '^\[(ERROR|FATAL)\]' "$log" | grep -v -E "$ignored_errors" || true)"
    if [ -n "$errors" ]; then
        echo "[STRESS TEST FAILED] render.thread=$mode : the engine logged errors :" >&2
        echo "$errors" >&2
        failed=1
    fi

    failed_commands="$(grep -E 'Script @[0-9]+ > .* FAILED' "$log" || true)"
    if [ -n "$failed_commands" ]; then
        echo "[STRESS TEST FAILED] render.thread=$mode : a scripted command failed :" >&2
        echo "$failed_commands" >&2
        failed=1
    fi

    if grep -q "RenderThread\]" "$log"; then
        echo "[STRESS TEST FAILED] render.thread=$mode : graphics calls from the wrong thread :" >&2
        grep "RenderThread\]" "$log" >&2
        failed=1
    fi
done

[ "$failed" -ne 0 ] && exit 1
echo "[STRESS TEST PASSED] with and without the render thread : no crash, no error, no call from the wrong thread."
