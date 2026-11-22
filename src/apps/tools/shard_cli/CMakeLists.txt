add_executable(ShardCli ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

target_compile_definitions(ShardCli PRIVATE BUILD_ENGINE SHARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(ShardCli PRIVATE ${CMAKE_SOURCE_DIR}/src/ ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/submodules/json/single_include/)
target_link_libraries(ShardCli PRIVATE Engine)

if(MINGW)
    target_link_options(ShardCli PRIVATE -static)
endif()

set_target_properties(ShardCli PROPERTIES
    OUTPUT_NAME shard
    WIN32_EXECUTABLE FALSE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
