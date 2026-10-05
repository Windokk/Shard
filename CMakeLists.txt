cmake_minimum_required(VERSION 3.28.2)

project(Shard VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COLOR_MAKEFILE ON)

if(UNIX)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
endif()

set(CMAKE_BUILD_TYPE Debug)

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

set(FMOD_API_DIR "" CACHE PATH "Path to fmod api")

configure_file(${CMAKE_SOURCE_DIR}/src/config.h.in config.h)

include_directories(${CMAKE_BINARY_DIR})

# Subdirs (Third party)
add_subdirectory(submodules/freetype)
add_subdirectory(submodules/glfw)
add_subdirectory(submodules/glm)
add_subdirectory(submodules/jolt/Build)

# Subdirs (Shard)
add_subdirectory(src/engine)
add_subdirectory(src/apps/game)
add_subdirectory(src/apps/editor)
add_subdirectory(src/apps/player)

enable_testing()
add_subdirectory(tests)