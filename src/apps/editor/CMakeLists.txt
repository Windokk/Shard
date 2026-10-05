file(GLOB_RECURSE EDITOR_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imguizmo/ImGuizmo.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui_demo.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui_widgets.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui_tables.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui_draw.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imgui/backends/imgui_impl_glfw.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imgui/backends/imgui_impl_opengl3.cpp
)

add_executable(ShardEditor ${EDITOR_SOURCES})

target_compile_definitions(ShardEditor PRIVATE BUILD_ENGINE BUILD_EDITOR)

target_include_directories(ShardEditor PRIVATE
  ${CMAKE_SOURCE_DIR}/src/
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/include/
  ${CMAKE_SOURCE_DIR}/submodules/imgui/
  ${CMAKE_SOURCE_DIR}/submodules/imguizmo/
  ${CMAKE_SOURCE_DIR}/submodules/glfw/include/
)

set_target_properties(ShardEditor PROPERTIES
    ${BUNDLE_ID_OPTION}
    WIN32_EXECUTABLE FALSE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

set(GLFW_LIB_DIR ${CMAKE_BINARY_DIR})

target_link_directories(ShardEditor PUBLIC ${GLFW_LIB_DIR})

target_link_libraries(ShardEditor PRIVATE Engine glfw3)