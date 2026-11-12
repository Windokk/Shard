#!/bin/bash
# Boots ShardGame on example_project, renders a few frames, fails on a crash, a hang or an [ERROR]/[FATAL] log line.
set -uo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
world="${SHARD_SMOKE_WORLD:-cornell.world}"
frames="${SHARD_SMOKE_FRAMES:-30}"
timeout_seconds="${SHARD_SMOKE_TIMEOUT:-300}"
api="${SHARD_SMOKE_API:-opengl}"
# A world without a camera (cornell.world) draws nothing at all : "px py pz tx ty tz [fov]" gives it one, see render.camera. Empty = the world's own.
view="${SHARD_SMOKE_VIEW-}"

# The CI has no sound card, so audio init errors are ignored.
ignored_errors='ma_engine_init failed'

cd "$root/build" || { echo "[ERROR] no build/ folder : build the project first." >&2; exit 1; }

# Engine resources are copied by hand (README), a fresh CI build lacks them.
for dir in engine_resources editor_resources; do
    if [ ! -d "$dir" ]; then
        cp -r "../resources/$dir" "$dir" || { echo "[ERROR] cannot copy resources/$dir to build/." >&2; exit 1; }
    fi
done

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

wrapper=()
if [ "$(uname -s)" = "Linux" ] && [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    if ! command -v xvfb-run > /dev/null 2>&1; then
        echo "[ERROR] no display and no xvfb-run : install xvfb (sudo apt install xvfb), or run this on a desktop." >&2
        exit 1
    fi
    wrapper=(xvfb-run --auto-servernum --server-args="-screen 0 1280x720x24")
fi

log="$(mktemp)"
script="$(mktemp)"
trap 'rm -f "$log" "$script"' EXIT

script_args=()
if [ -n "$view" ]; then
    echo "5 render.camera $view" > "$script"
    script_args=(--script "$script")
fi

if [ "$api" = "vulkan" ] && [ -n "${SHARD_SMOKE_VALIDATION-1}" ]; then
    export SHARD_VULKAN_VALIDATION=1
fi

echo "[INFO] Smoke test : $world of example_project, $frames frames, $api${view:+, camera $view}."

# --vsync turns vsync off, nothing to wait for on a virtual display. stdin is closed so nothing waits for Enter.
timeout "$timeout_seconds" "${wrapper[@]}" "$player" \
    --game "$module" \
    --project ../example_project/example_project.json \
    --api "$api" \
    "${script_args[@]}" \
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

# Mesa software GL lacks GL_ARB_bindless_texture, which lit.frag needs (SSAO handle), so its shader compile always fails there.
# When the driver says so, the generic compile/link lines are not counted. A real shader error is hidden too.
if [ "$api" = "opengl" ] && grep -q "GL_ARB_bindless_texture' unsupported" "$log"; then
    echo "[WARNING] The OpenGL driver has no GL_ARB_bindless_texture : shader compile / link errors are ignored by this smoke test."
    ignored_errors="$ignored_errors|Couldn't compile shader|Couldn't link shader"
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

echo "[SMOKE TEST PASSED] example_project / $world ($api) : $frames frames rendered, clean shutdown, no error logged."
