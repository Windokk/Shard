#!/bin/bash
# Smoke test : starts the player (ShardGame) on example_project, loads a world, renders a few frames and shuts down.
# It fails when the player crashes, hangs, does not get to the end, or logs an [ERROR] / [FATAL] line (a file that
# cannot be read, a shader that does not compile...). It does not look at the picture : it is there to catch what
# breaks the engine at boot. Nothing is written to the project (see --frames in src/apps/player/main.cpp).
#
# Usage : bash scripts/smoke_test.sh     (after a build : it uses build/ShardGame and build/libGameModule)
#
#   SHARD_SMOKE_WORLD    the world to load (default cornell.world : Sponza, the first world of the project, is a Git LFS
#                        file a plain checkout does not have, and it is far too heavy for a software OpenGL)
#   SHARD_SMOKE_FRAMES   how many frames to render (default 30)
#   SHARD_SMOKE_TIMEOUT  seconds before the run is given up as hung (default 300)
#
# On Linux without a display it starts a virtual one (xvfb-run) : OpenGL is then Mesa's software renderer (llvmpipe).
set -uo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
world="${SHARD_SMOKE_WORLD:-cornell.world}"
frames="${SHARD_SMOKE_FRAMES:-30}"
timeout_seconds="${SHARD_SMOKE_TIMEOUT:-300}"

# Errors that say something about the machine, not about the engine : the CI has no sound card.
ignored_errors='ma_engine_init failed'

cd "$root/build" || { echo "[ERROR] no build/ folder : build the project first." >&2; exit 1; }

# The engine resources are copied next to the executable by hand (README) : a fresh CI build does not have them
for dir in engine_resources editor_resources; do
    if [ ! -d "$dir" ]; then
        cp -r "../resources/$dir" "$dir" || { echo "[ERROR] cannot copy resources/$dir to build/." >&2; exit 1; }
    fi
done

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

# A window needs a display : make one when there is none (the CI)
wrapper=()
if [ "$(uname -s)" = "Linux" ] && [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    if ! command -v xvfb-run > /dev/null 2>&1; then
        echo "[ERROR] no display and no xvfb-run : install xvfb (sudo apt install xvfb), or run this on a desktop." >&2
        exit 1
    fi
    wrapper=(xvfb-run --auto-servernum --server-args="-screen 0 1280x720x24")
fi

log="$(mktemp)"
trap 'rm -f "$log"' EXIT

echo "[INFO] Smoke test : $world of example_project, $frames frames."

# --vsync switches the vertical sync OFF : nothing to wait for on a virtual display. stdin is closed so that nothing can wait for Enter.
timeout "$timeout_seconds" "${wrapper[@]}" "$player" \
    --game "$module" \
    --project ../example_project/example_project.json \
    --api opengl \
    --world "$world" \
    --frames "$frames" \
    --width 640 --height 360 \
    --vsync \
    < /dev/null 2>&1 | tee "$log"
status="${PIPESTATUS[0]}"

failed=0

if [ "$status" -eq 124 ]; then
    echo "[SMOKE TEST FAILED] No result after ${timeout_seconds}s : the player hangs (or the machine is too slow : SHARD_SMOKE_TIMEOUT)." >&2
    failed=1
elif [ "$status" -ne 0 ]; then
    echo "[SMOKE TEST FAILED] The player exited with code $status (log above)." >&2
    failed=1
elif ! grep -q "shut down cleanly" "$log"; then
    echo "[SMOKE TEST FAILED] The player exited with 0 without going through its shutdown (log above)." >&2
    failed=1
fi

errors="$(grep -E '^\[(ERROR|FATAL)\]' "$log" | grep -v -E "$ignored_errors" || true)"
if [ -n "$errors" ]; then
    echo "[SMOKE TEST FAILED] The engine logged errors :" >&2
    echo "$errors" >&2
    failed=1
fi

if [ "$failed" -ne 0 ]; then
    exit 1
fi

echo "[SMOKE TEST PASSED] example_project / $world : $frames frames rendered, clean shutdown, no error logged."
