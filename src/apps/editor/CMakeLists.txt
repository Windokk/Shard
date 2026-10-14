file(GLOB_RECURSE EDITOR_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imguizmo/ImGuizmo.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui_demo.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui_widgets.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui_tables.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imgui/imgui_draw.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imgui/backends/imgui_impl_sdl3.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imgui/backends/imgui_impl_opengl3.cpp
  ${CMAKE_SOURCE_DIR}/submodules/imgui/backends/imgui_impl_vulkan.cpp
)

add_executable(ShardEditor ${EDITOR_SOURCES})

# The interface draws on the Vulkan backend with the entry points volk loaded for the engine (see ShardRHI)
target_compile_definitions(ShardEditor PRIVATE BUILD_ENGINE BUILD_EDITOR IMGUI_IMPL_VULKAN_USE_VOLK)

target_include_directories(ShardEditor PRIVATE
  ${CMAKE_SOURCE_DIR}/src/
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/include/
  ${CMAKE_SOURCE_DIR}/submodules/imgui/
  ${CMAKE_SOURCE_DIR}/submodules/imguizmo/
)

set_target_properties(ShardEditor PROPERTIES
    ${BUNDLE_ID_OPTION}
    WIN32_EXECUTABLE FALSE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_link_libraries(ShardEditor PRIVATE Engine)
