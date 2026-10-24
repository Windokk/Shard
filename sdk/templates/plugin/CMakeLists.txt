# A Shard plugin built against the Shard SDK alone : no engine source, no engine library.
#
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=<the SDK folder, e.g. <engine install>/sdk>
#   cmake --build build
#
# The plugin lands in build/plugins/<id>/ : give that folder to the engine (ShardHeadless --plugin build/plugins/<id>, or copy it into the
# project's plugins folder).
cmake_minimum_required(VERSION 3.20)
project(ShardPluginTemplate LANGUAGES CXX)

find_package(ShardSDK 1.0 REQUIRED)

# native : a library the engine loads (a native plugin, signed and consented to). wasm : the same sources compiled to WebAssembly, a sandboxed
# plugin (tier 2) the engine loads without either, and that can do nothing the host does not give it. It needs a wasi-sdk 34 or later :
#   cmake -S . -B build -DSHARD_PLUGIN_TARGET=wasm -DSHARD_WASM_TOOLCHAIN=<wasi-sdk or its sysroot> -DCMAKE_PREFIX_PATH=...
set(SHARD_PLUGIN_TARGET native CACHE STRING "native or wasm")

if(SHARD_PLUGIN_TARGET STREQUAL "wasm")
    shard_sdk_add_plugin(template_plugin
        TARGET      wasm
        MANIFEST    shard-plugin.json
        SOURCE_DIRS src
        CONTENT_DIR content
    )
else()
    shard_sdk_add_plugin(template_plugin
        MANIFEST    shard-plugin.json
        SOURCE_DIRS src          # every .cpp under src/ : add a file, run cmake again (or just build), and it is part of the plugin
        CONTENT_DIR content
        PACKAGE                 # cmake --build build --target template_plugin_package : build/plugins/<id>.shardplugin, with the SDK's ShardPkg
    )
endif()
