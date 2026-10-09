#!/bin/bash
# Regenerates the *.reflection.hpp files with ShardReflect : one next to every header that has CLASS() / STRUCT()
# annotations. The files are committed, so this has to be run (and its result committed) whenever a reflected class
# changes. The CI runs it as well and fails when its output differs from what is committed (.github/workflows/reflection.yml).
#
# Usage : bash scripts/regen_reflection.sh [path/to/ShardReflect]   (default : build/tools/ShardReflect[.exe])
#
# Needs clang (the compiler's own headers) and g++ (the C++ standard library) on the PATH, as ShardReflect parses the
# headers with them. When they are not where this script looks for them, set SHARD_CLANG_RESOURCE_DIR (the output of
# `clang -print-resource-dir`) and SHARD_CPP_DIR (usually /usr/include/c++/<version>) yourself.
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

# ShardReflect resolves everything (-f, -I, the files it writes) from the working directory
cd "$(dirname "$0")/.."

# Native Windows tools (MinGW's clang, ShardReflect.exe) do not understand the POSIX paths of an MSYS2 shell
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
    # <prefix>/bin/g++ -> <prefix>/include/c++/<version> (/mingw64 on MSYS2, /usr on Debian / Ubuntu)
    cpp_dir="$(dirname "$(dirname "$(command -v g++)")")/include/c++/$(g++ -dumpversion)"
    if [ ! -d "$cpp_dir" ]; then
        echo "[ERROR] $cpp_dir does not exist : set SHARD_CPP_DIR to the libstdc++ headers." >&2
        exit 1
    fi
    cpp_dir="$(native_path "$cpp_dir")"
fi

includes="src;submodules/;submodules/json/single_include;submodules/jolt;submodules/glm;submodules/freetype/include"

# Every header that declares something to reflect. attributes.hpp defines the macros and the *.reflection.hpp files are
# the output : neither has to be parsed.
mapfile -t headers < <(grep -rlE '\b(CLASS|STRUCT)\(' src --include='*.hpp' --exclude='*.reflection.hpp' --exclude='attributes.hpp' | sort)
if [ "${#headers[@]}" -eq 0 ]; then
    echo "[ERROR] no header with CLASS() / STRUCT() found under src/." >&2
    exit 1
fi

for header in "${headers[@]}"; do
    "$tool" --clang "$clang_dir" --cpp "$cpp_dir" -I "$includes" -f "$header"
done

echo "[INFO] Reflection regenerated for ${#headers[@]} headers."
