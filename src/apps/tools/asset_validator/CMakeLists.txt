add_executable(ShardAssetValidator ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

target_compile_definitions(ShardAssetValidator PRIVATE BUILD_ENGINE)
target_include_directories(ShardAssetValidator PRIVATE ${CMAKE_SOURCE_DIR}/src/ ${CMAKE_SOURCE_DIR}/submodules/json/single_include/)
target_link_libraries(ShardAssetValidator PRIVATE Engine)

set_target_properties(ShardAssetValidator PROPERTIES
    WIN32_EXECUTABLE FALSE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
