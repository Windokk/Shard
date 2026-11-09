add_executable(ShardCooker ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

target_compile_definitions(ShardCooker PRIVATE BUILD_ENGINE)
target_include_directories(ShardCooker PRIVATE ${CMAKE_SOURCE_DIR}/src/ ${CMAKE_SOURCE_DIR}/submodules/json/single_include/)
# The engine : textures are compressed and models built by the same code the loaders use. No window or GPU is needed.
target_link_libraries(ShardCooker PRIVATE Engine)

set_target_properties(ShardCooker PROPERTIES
    WIN32_EXECUTABLE FALSE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
