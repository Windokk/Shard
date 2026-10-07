add_executable(ShardGame
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/player_window.cpp
)

target_compile_definitions(ShardGame PRIVATE BUILD_ENGINE BUILD_GAME)

target_include_directories(ShardGame PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/include/
  ${CMAKE_SOURCE_DIR}/src/
)

target_link_libraries(ShardGame PRIVATE Engine)

set_target_properties(ShardGame PROPERTIES
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)