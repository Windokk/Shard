cmake_minimum_required(VERSION 3.20)
project(ShardPluginTemplate LANGUAGES CXX)

find_package(ShardSDK 1.0 REQUIRED)

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
        SOURCE_DIRS src
        CONTENT_DIR content
        PACKAGE
    )
endif()
