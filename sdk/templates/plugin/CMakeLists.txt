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

shard_sdk_add_plugin(template_plugin
    MANIFEST    shard-plugin.json
    SOURCES     src/plugin.cpp
    CONTENT_DIR content
    PACKAGE                 # cmake --build build --target template_plugin_package : build/plugins/<id>.shardplugin, with the SDK's ShardPkg
)
