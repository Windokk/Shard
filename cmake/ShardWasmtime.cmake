# Wasmtime : the WebAssembly runtime of sandboxed (tier 2) plugins, docs/design-notes/wasm-sandbox.fr.md (W1b).
# The prebuilt C API of a pinned release, downloaded at configure time and checked by its SHA-256 : no Rust toolchain is needed. Shared : on
# Windows wasmtime.dll is copied next to the executables (like SDL3.dll), on Linux the build rpath points at libwasmtime.so.
# To move to another release : change the version and the three hashes (sha256sum of the archives of the GitHub release).
include(FetchContent)

set(SHARD_WASMTIME_VERSION "50.0.0")
if(WIN32 AND MINGW)
    set(_shard_wasmtime_asset "x86_64-mingw-c-api.zip")
    set(_shard_wasmtime_sha256 "1b4dcf6d8a7b933136a1eaff708fc34ac6954efb3b105d39474aac99dee09635")
elseif(WIN32)
    set(_shard_wasmtime_asset "x86_64-windows-c-api.zip")
    set(_shard_wasmtime_sha256 "3c3d04bc910d18fac9f85ad11293aab63ea023fff5021d39be858d4e3b9d058f")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(_shard_wasmtime_asset "x86_64-linux-c-api.tar.xz")
    set(_shard_wasmtime_sha256 "631643d8be5dac2a1abe026528f0ca18a515bce275bcfa7eb9731c9468a592f1")
else()
    message(FATAL_ERROR "Wasmtime : no prebuilt C API is pinned for ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR} (cmake/ShardWasmtime.cmake)")
endif()

FetchContent_Declare(wasmtime
    URL "https://github.com/bytecodealliance/wasmtime/releases/download/v${SHARD_WASMTIME_VERSION}/wasmtime-v${SHARD_WASMTIME_VERSION}-${_shard_wasmtime_asset}"
    URL_HASH SHA256=${_shard_wasmtime_sha256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(wasmtime)       # the archive has no CMakeLists.txt : this only downloads and unpacks it

add_library(ShardWasmtime SHARED IMPORTED GLOBAL)
set_target_properties(ShardWasmtime PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${wasmtime_SOURCE_DIR}/include")
if(WIN32)
    file(GLOB _shard_wasmtime_implib "${wasmtime_SOURCE_DIR}/lib/libwasmtime.dll.a" "${wasmtime_SOURCE_DIR}/lib/wasmtime.dll.lib")
    set_target_properties(ShardWasmtime PROPERTIES
        IMPORTED_LOCATION "${wasmtime_SOURCE_DIR}/lib/wasmtime.dll"
        IMPORTED_IMPLIB "${_shard_wasmtime_implib}")
    file(COPY "${wasmtime_SOURCE_DIR}/lib/wasmtime.dll" DESTINATION "${CMAKE_BINARY_DIR}")
else()
    # The prebuilt .so carries no SONAME : without NO_SONAME the linker records the (build-relative) path as the NEEDED entry, which the
    # loader fails to resolve from another working directory (ctest). NO_SONAME makes CMake link with -L/-l instead (bare NEEDED, found via rpath).
    set_target_properties(ShardWasmtime PROPERTIES
        IMPORTED_LOCATION "${wasmtime_SOURCE_DIR}/lib/libwasmtime.so"
        IMPORTED_NO_SONAME TRUE)
endif()
