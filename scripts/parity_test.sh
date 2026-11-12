#!/bin/bash
# Renders a world twice and fails when the pictures differ more than the limits (compare_frames.py apis : a blank picture fails too).
#   bash scripts/parity_test.sh pipelines   forward+ against deferred on Vulkan, physics.world (what the CI runs, on a software Vulkan)
#   bash scripts/parity_test.sh apis        OpenGL against Vulkan and the path tracer, sponza.world (needs a GPU : llvmpipe has no bindless textures)
# Variables : SHARD_PARITY_WORLD, _WARMUP, _TIMEOUT, _MAX_MEAN, _MAX_P99, _MAX_OVER, _MAX_RT_BIAS, _ARGS (more compare_frames.py options).
# Pictures and difference maps go to build/api_compare/<world>_<mode>/. Limits come from runs on an RTX 5060 (0.15 to 0.3 of 255 on Sponza).
set -uo pipefail

mode="${1:-pipelines}"
root="$(cd "$(dirname "$0")/.." && pwd)"
extra=()

case "$mode" in
    pipelines)
        world="${SHARD_PARITY_WORLD:-physics.world}"
        sides=(--a vulkan:forward_plus --b vulkan:deferred)
        warmup="${SHARD_PARITY_WARMUP:-40}"
        max_mean="${SHARD_PARITY_MAX_MEAN:-0.5}"
        max_p99="${SHARD_PARITY_MAX_P99:-4}"
        max_over="${SHARD_PARITY_MAX_OVER:-0.5}"
        export SHARD_VULKAN_VALIDATION="${SHARD_VULKAN_VALIDATION:-1}"
        ;;
    apis)
        world="${SHARD_PARITY_WORLD:-sponza.world}"
        sides=(--a opengl --b vulkan)
        warmup="${SHARD_PARITY_WARMUP:-60}"
        max_mean="${SHARD_PARITY_MAX_MEAN:-0.6}"
        max_p99="${SHARD_PARITY_MAX_P99:-6}"
        max_over="${SHARD_PARITY_MAX_OVER:-1}"
        extra=(--raytrace "${SHARD_PARITY_RT_SPP:-32}" "${SHARD_PARITY_RT_BOUNCES:-3}" --max-rt-bias "${SHARD_PARITY_MAX_RT_BIAS:-0.5}")
        ;;
    *)
        echo "usage: bash scripts/parity_test.sh [pipelines|apis]" >&2
        exit 2
        ;;
esac

cd "$root/build" || { echo "[ERROR] no build/ folder : build the project first." >&2; exit 1; }

for dir in engine_resources editor_resources; do
    if [ ! -d "$dir" ]; then
        cp -r "../resources/$dir" "$dir" || { echo "[ERROR] cannot copy resources/$dir to build/." >&2; exit 1; }
    fi
done
cd "$root" || exit 1

wrapper=()
if [ "$(uname -s)" = "Linux" ] && [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    if ! command -v xvfb-run > /dev/null 2>&1; then
        echo "[ERROR] no display and no xvfb-run : install xvfb (sudo apt install xvfb), or run this on a desktop." >&2
        exit 1
    fi
    wrapper=(xvfb-run --auto-servernum --server-args="-screen 0 1280x720x24")
fi

python=python3
command -v python3 > /dev/null 2>&1 || python=python

name="${world%.world}_$mode"
echo "[INFO] Parity test ($mode) : $world, limits mean $max_mean, p99 $max_p99, over-8 $max_over %."

# shellcheck disable=SC2086
"${wrapper[@]}" "$python" scripts/compare_frames.py apis --world "$world" "${sides[@]}" --warmup "$warmup" \
    --timeout "${SHARD_PARITY_TIMEOUT:-900}" --out "build/api_compare/$name" \
    --max-mean "$max_mean" --max-p99 "$max_p99" --max-over "$max_over" "${extra[@]}" ${SHARD_PARITY_ARGS:-}
status=$?

if [ "$status" -ne 0 ]; then
    echo "[PARITY TEST FAILED] $mode on $world (exit $status) : pictures and difference maps in build/api_compare/$name/" >&2
    exit 1
fi
echo "[PARITY TEST PASSED] $mode on $world"
