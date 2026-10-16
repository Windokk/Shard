cmake_minimum_required(VERSION 3.28.2)

project(Shard VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COLOR_MAKEFILE ON)

if(UNIX)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
endif()

# Debug unless the caller picks one (cmake -DCMAKE_BUILD_TYPE=Release ..., or `build.bat Release`) : Debug is -O0, which
# is far slower than Release for the engine and Jolt alike.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# --- Unified build output directory ---
set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build")

# Executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

# Shared libraries (.so / .dll)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

# Static libraries (.a / .lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

# For multi-config generators (Xcode / Visual Studio)
foreach(OUTPUTCONFIG Debug Release RelWithDebInfo MinSizeRel)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${OUTPUT_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${OUTPUT_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${OUTPUT_DIR}")
endforeach()

configure_file(${CMAKE_SOURCE_DIR}/src/config.h.in config.h)

include_directories(${CMAKE_BINARY_DIR})

# -Wall -Wextra on Shard's own targets, see cmake/ShardWarnings.cmake
include(${CMAKE_SOURCE_DIR}/cmake/ShardWarnings.cmake)

# Subdirs (Third party)
add_subdirectory(submodules/freetype SYSTEM)

# SDL3 : window system, input, clipboard, dialogs (only the platform layer sees it). Shared, so that SDL3.dll sits next to the executables.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(submodules/SDL EXCLUDE_FROM_ALL SYSTEM)
add_subdirectory(submodules/glm SYSTEM)

# glslang : turns the GLSL of the engine into SPIR-V for the Vulkan backend (and reflects it, with SPIRV-Reflect). Nothing but the compiler : no
# SPIRV-Tools optimizer, no standalone programs. Used by ShardShaderCompiler (the shaderc tool, and the runtime when it has to compile a shader
# nobody compiled before : hot reload). (Slang does not go through it : slangc compiles those, see shader_compiler.cpp.)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
add_subdirectory(submodules/glslang EXCLUDE_FROM_ALL SYSTEM)

# Vulkan : the headers, the loader (volk loads vulkan-1.dll / libvulkan.so at run time : nothing links against the SDK) and the
# bootstrap that picks the device and builds the swapchain. The memory allocator (VMA) is a header, see src/engine/CMakeLists.txt.
set(VK_BOOTSTRAP_VULKAN_HEADER_DIR "${CMAKE_SOURCE_DIR}/submodules/Vulkan-Headers/include" CACHE FILEPATH "" FORCE)
set(VK_BOOTSTRAP_TEST OFF CACHE BOOL "" FORCE)
set(VK_BOOTSTRAP_INSTALL OFF CACHE BOOL "" FORCE)
set(VK_BOOTSTRAP_DISABLE_WARNINGS ON CACHE BOOL "" FORCE)
add_subdirectory(submodules/vk-bootstrap EXCLUDE_FROM_ALL SYSTEM)
# Determinism (see docs, "Headless & determinism") : a replay has to give the same state on Windows and on Linux. Jolt then
# is built in its cross platform deterministic mode (no fused multiply-add, its own sin/cos... a little slower), and the
# compiler is told not to fuse a * b + c on its own, which would round differently from one target to the next.
option(SHARD_DETERMINISTIC_PHYSICS "Build Jolt in cross platform deterministic mode (same simulation on every platform)" ON)
if(SHARD_DETERMINISTIC_PHYSICS)
    set(CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "Cross platform deterministic" FORCE)
    if(NOT MSVC)
        add_compile_options(-ffp-contract=off)
    endif()
endif()

# Jolt with RTTI, like the rest of the engine. Without it libJolt has no typeinfo for the Jolt classes that define a
# virtual function out of line (JobSystemWithBarrier...), and a class of ours deriving from one (JoltJobSystem) fails to
# link on Linux ("undefined reference to typeinfo for JPH::JobSystemWithBarrier").
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
add_subdirectory(submodules/jolt/Build SYSTEM)

# Tools (ShardReflect, the reflection generator)
add_subdirectory(src/apps/tools/reflect)

# Subdirs (Shard)
add_subdirectory(src/engine)
add_subdirectory(src/apps/tools/shaderc)
add_subdirectory(src/apps/game)
add_subdirectory(src/apps/editor)
add_subdirectory(src/apps/player)
add_subdirectory(src/apps/headless)

enable_testing()
add_subdirectory(tests)

shard_enable_warnings(
    ShardCore ShardPlatform ShardAssets ShardWorld ShardPhysics ShardAudio ShardShaderCompiler ShardRHI ShardRenderer ShardEngine
    GameModule ShardEditor ShardGame ShardHeadless ShardShaderc ShardTests
)