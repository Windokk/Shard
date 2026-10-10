set -euo pipefail

file="$(dirname "$0")/../submodules/imgui/backends/imgui_impl_vulkan.h"

if [ ! -f "$file" ]; then
    echo "[ERROR] $file not found : run 'git submodule update --init --recursive' first." >&2
    exit 1
fi

sed -i 's|#include <vulkan/vulkan.h>|#include "engine/renderer/rhi/backends/glad/include/glad/vulkan.h"|' "$file"

if ! grep -q 'glad/vulkan.h' "$file"; then
    echo "[ERROR] imgui patch not applied : imgui_impl_vulkan.h no longer has the include this script replaces." >&2
    exit 1
fi
