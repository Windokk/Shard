# shardpkg : packs, signs, verifies and indexes plugins (see main.cpp). Needs nothing of the engine's running parts : just the plugin code.
add_executable(ShardPkg ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

target_compile_definitions(ShardPkg PRIVATE BUILD_ENGINE)
target_include_directories(ShardPkg PRIVATE ${CMAKE_SOURCE_DIR}/src/ ${CMAKE_SOURCE_DIR}/submodules/json/single_include/)
target_link_libraries(ShardPkg PRIVATE Engine)

# It ships in the SDK (sdk/bin, for shard_sdk_add_plugin(... PACKAGE)) : on its own there, so it carries the compiler's runtime
if(MINGW)
    target_link_options(ShardPkg PRIVATE -static)
endif()

set_target_properties(ShardPkg PROPERTIES
    WIN32_EXECUTABLE FALSE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
