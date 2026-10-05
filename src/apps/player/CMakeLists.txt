file(GLOB SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/glfw/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/platform/glfw/*.cpp
)

add_executable(ShardGame
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${SOURCES}
)

set(GLFW_LIB_DIR ${CMAKE_BINARY_DIR})

target_compile_definitions(ShardGame PRIVATE BUILD_ENGINE BUILD_GAME GLFW_STATIC GLFW_INCLUDE_NONE)

target_include_directories(ShardGame PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/include/
  ${CMAKE_SOURCE_DIR}/src/
  ${CMAKE_SOURCE_DIR}/submodules/glfw/include/
)

target_link_directories(ShardGame PUBLIC ${GLFW_LIB_DIR})
target_link_libraries(ShardGame PRIVATE Engine glfw3)

set_target_properties(ShardGame PROPERTIES
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)