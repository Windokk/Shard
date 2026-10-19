# asset_validator : checks a project (its file, its plugins, its asset database, its worlds) without starting the engine, and runs the checks
# plugins add (see main.cpp). Exit code 0 : nothing wrong.
add_executable(ShardAssetValidator ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

target_compile_definitions(ShardAssetValidator PRIVATE BUILD_ENGINE)
target_include_directories(ShardAssetValidator PRIVATE ${CMAKE_SOURCE_DIR}/src/ ${CMAKE_SOURCE_DIR}/submodules/json/single_include/)
target_link_libraries(ShardAssetValidator PRIVATE Engine)

set_target_properties(ShardAssetValidator PROPERTIES
    WIN32_EXECUTABLE FALSE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
