#!/bin/bash
# Regenerates the committed *.reflection.hpp. CI reruns it and fails if the output differs (.github/workflows/reflection.yml).
# Needs clang and g++ on PATH, or set SHARD_CLANG_RESOURCE_DIR and SHARD_CPP_DIR.
set -euo pipefail

tool="${1:-}"
if [ -z "$tool" ]; then
    for candidate in build/tools/ShardReflect.exe build/tools/ShardReflect; do
        if [ -x "$(dirname "$0")/../$candidate" ]; then
            tool="$(dirname "$0")/../$candidate"
            break
        fi
    done
fi
if [ -z "$tool" ] || [ ! -x "$tool" ]; then
    echo "[ERROR] ShardReflect not found : build it first (cmake --build build --target ShardReflect, needs LLVM + Clang)," >&2
    echo "        or pass its path as the first argument." >&2
    exit 1
fi
tool="$(realpath "$tool")"

cd "$(dirname "$0")/.."

# Native Windows tools (MinGW clang, ShardReflect.exe) do not understand MSYS2 POSIX paths.
native_path() {
    if command -v cygpath > /dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi
}

clang_dir="${SHARD_CLANG_RESOURCE_DIR:-}"
if [ -z "$clang_dir" ]; then
    if ! command -v clang > /dev/null 2>&1; then
        echo "[ERROR] clang not found on the PATH : install it, or set SHARD_CLANG_RESOURCE_DIR." >&2
        exit 1
    fi
    clang_dir="$(native_path "$(clang -print-resource-dir)")"
fi

cpp_dir="${SHARD_CPP_DIR:-}"
if [ -z "$cpp_dir" ]; then
    if ! command -v g++ > /dev/null 2>&1; then
        echo "[ERROR] g++ not found on the PATH : install it, or set SHARD_CPP_DIR." >&2
        exit 1
    fi
    cpp_dir="$(dirname "$(dirname "$(command -v g++)")")/include/c++/$(g++ -dumpversion)"
    if [ ! -d "$cpp_dir" ]; then
        echo "[ERROR] $cpp_dir does not exist : set SHARD_CPP_DIR to the libstdc++ headers." >&2
        exit 1
    fi
    cpp_dir="$(native_path "$cpp_dir")"
fi

includes="src;submodules/;submodules/json/single_include;submodules/jolt;submodules/glm;submodules/freetype/include"

mapfile -t headers < <(grep -rlE '\b(CLASS|STRUCT)\(' src --include='*.hpp' --exclude='*.reflection.hpp' --exclude='attributes.hpp' | sort)
if [ "${#headers[@]}" -eq 0 ]; then
    echo "[ERROR] no header with CLASS() / STRUCT() found under src/." >&2
    exit 1
fi

for header in "${headers[@]}"; do
    "$tool" --clang "$clang_dir" --cpp "$cpp_dir" -I "$includes" -f "$header"
done

echo "[INFO] Reflection regenerated for ${#headers[@]} headers."
