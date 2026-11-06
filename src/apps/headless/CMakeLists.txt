# Simulation without a window, GPU or sound card (dedicated server, CI replay runner). Same engine, game module and simulation code as the player, on the null graphics backend and headless platform.
add_executable(ShardHeadless
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_compile_definitions(ShardHeadless PRIVATE BUILD_ENGINE BUILD_GAME)

target_include_directories(ShardHeadless PRIVATE
  ${CMAKE_SOURCE_DIR}/src/
)

target_link_libraries(ShardHeadless PRIVATE Engine)

set_target_properties(ShardHeadless PROPERTIES
    WIN32_EXECUTABLE FALSE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
