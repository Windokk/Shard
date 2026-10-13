# ShardHeadless : the simulation without a window, a GPU or a sound card (the dedicated server, the replay runner of the CI).
# Same engine, same game module, same simulation code as the player : it only boots on the null graphics backend and the
# headless platform instead of SDL + OpenGL.
add_executable(ShardHeadless
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_compile_definitions(ShardHeadless PRIVATE BUILD_ENGINE BUILD_GAME)

target_include_directories(ShardHeadless PRIVATE
  ${CMAKE_SOURCE_DIR}/src/
)

target_link_libraries(ShardHeadless PRIVATE Engine)

# A console program : its output is what a script or a CI reads
set_target_properties(ShardHeadless PROPERTIES
    WIN32_EXECUTABLE FALSE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
