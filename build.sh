#!/bin/bash
set -uo pipefail
cd "$(dirname "$0")"

bash scripts/patch_imgui.sh || exit 1

cmake -S . -B build -G "Unix Makefiles" ${1:+-DCMAKE_BUILD_TYPE=$1}
if [ $? -ne 0 ]; then
    echo "[ERROR] CMake configuration failed."
    exit 1
fi

cmake --build build -j "$(nproc 2>/dev/null || echo 4)"
if [ $? -ne 0 ]; then
    echo "[ERROR] Build failed."
    exit 1
fi

if [ -n "${SHARD_NO_RUN:-}" ]; then
    exit 0
fi

echo "[INFO] Build succeeded. Starting editor..."
cd build
./ShardEditor --game plugins/game/bin/linux-x86_64/libGameModule.so --project ../example_project/example_project.json --api vulkan
