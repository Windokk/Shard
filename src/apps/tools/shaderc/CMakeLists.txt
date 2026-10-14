# shaderc : compiles the shaders of a directory to SPIR-V with their reflection (see main.cpp). Built with the engine, run by the
# ShardShaders target on resources/engine_resources/shaders.
find_package(Threads REQUIRED)
add_executable(ShardShaderc ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

target_compile_definitions(ShardShaderc PRIVATE BUILD_ENGINE)
target_include_directories(ShardShaderc PRIVATE ${CMAKE_SOURCE_DIR}/src/)
target_link_libraries(ShardShaderc PRIVATE ShardShaderCompiler Threads::Threads)

set_target_properties(ShardShaderc PROPERTIES
    WIN32_EXECUTABLE FALSE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# The SPIR-V of every engine shader, next to the GLSL the OpenGL backend reads : build/engine_resources/shaders_spv/<program>.spvpack
# (a program whose sources did not change is skipped, so this is cheap once it has run).
set(SHARD_SHADER_ROOT "${CMAKE_SOURCE_DIR}/resources/engine_resources/shaders")
set(SHARD_SHADER_OUT "${CMAKE_BINARY_DIR}/engine_resources/shaders_spv")
add_custom_target(ShardShaders
    COMMAND ShardShaderc "${SHARD_SHADER_ROOT}" "${SHARD_SHADER_OUT}" --quiet
    COMMENT "Compiling the shaders to SPIR-V"
    VERBATIM
)
add_dependencies(ShardShaders ShardShaderc)
