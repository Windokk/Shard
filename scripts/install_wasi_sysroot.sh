#!/usr/bin/env bash
# Downloads the wasi-sdk 34 sysroot (libc, libc++) and compiler-rt's builtins for wasm32, checks their SHA-256, and unpacks them in <dir>.
# What a sandboxed plugin (tier 2) is built with : shard_sdk_add_plugin(... TARGET wasm), see sdk/cmake/ShardSDKWasm.cmake.
#   bash scripts/install_wasi_sysroot.sh <dir>
# then   -DSHARD_WASM_TOOLCHAIN=<dir>/wasi-sysroot-34.0 -DSHARD_WASM_BUILTINS=<dir>/libclang_rt-34.0/wasm32-unknown-wasip1/libclang_rt.builtins.a
# (the tests use -DSHARD_TEST_WASM_TOOLCHAIN= for the toolchain). Clang and wasm-ld come from the system.
set -euo pipefail
dest="${1:?usage: install_wasi_sysroot.sh <dir>}"
base="https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-34"
mkdir -p "$dest"
fetch() {   # name sha256
    if [ ! -f "$dest/$1" ]; then curl -fsSL --retry 3 -o "$dest/$1" "$base/$1"; fi
    echo "$2  $dest/$1" | sha256sum -c - >/dev/null || { echo "$1 does not match its SHA-256" >&2; rm -f "$dest/$1"; exit 1; }
    tar xzf "$dest/$1" -C "$dest"
}
fetch wasi-sysroot-34.0.tar.gz 9d813544eeebe38b7b8f2244ed591de46b6db812c6dd1a257ff9f0d2a905a2be
fetch libclang_rt-34.0.tar.gz eee3e634dcf71aa22b1333391623cf5c9965a637dc428a27b1a858c026c587f1
echo "SHARD_WASM_TOOLCHAIN=$dest/wasi-sysroot-34.0"
echo "SHARD_WASM_BUILTINS=$dest/libclang_rt-34.0/wasm32-unknown-wasip1/libclang_rt.builtins.a"
