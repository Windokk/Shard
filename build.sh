#!/bin/bash
# Configure, build, then start the editor (usage: ./build.sh [Debug|Release|RelWithDebInfo] - omit to keep the build folder's current type)
# SHARD_NO_RUN=1 ./build.sh : build only, do not start the editor
set -uo pipefail
cd "$(dirname "$0")"

# The editor needs imgui to use the engine's vulkan.h (does nothing if it is already done)
bash scripts/patch_imgui.sh || exit 1

cmake -S . -B build -G "Unix Makefiles" ${1:+-DCMAKE_BUILD_TYPE=$1}
if [ $? -ne 0 ]; then
    echo "[ERROR] CMake configuration failed."
    exit 1
fi

# The reflection files (*.reflection.hpp) are committed : ShardReflect only has to be run by hand when a reflected class
# changes (see src/apps/tools/reflect/README.md)

cmake --build build -j "$(nproc 2>/dev/null || echo 4)"
if [ $? -ne 0 ]; then
    echo "[ERROR] Build failed."
    exit 1
fi

if [ -n "${SHARD_NO_RUN:-}" ]; then
    exit 0
fi

# Run the editor only if build succeeded
echo "[INFO] Build succeeded. Starting editor..."
cd build
./ShardEditor --game libGameModule.so --project ../example_project/example_project.json --api opengl
