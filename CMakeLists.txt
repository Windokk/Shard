cmake_minimum_required(VERSION 3.28.2)

project(Shard VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COLOR_MAKEFILE ON)

if(UNIX)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

foreach(OUTPUTCONFIG Debug Release RelWithDebInfo MinSizeRel)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${OUTPUT_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${OUTPUT_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${OUTPUT_DIR}")
endforeach()

configure_file(${CMAKE_SOURCE_DIR}/src/config.h.in config.h)

include_directories(${CMAKE_BINARY_DIR})

include(${CMAKE_SOURCE_DIR}/cmake/ShardWarnings.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/ShardPlugin.cmake)

add_subdirectory(submodules/freetype SYSTEM)

include(${CMAKE_SOURCE_DIR}/cmake/ShardWasmtime.cmake)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(submodules/SDL EXCLUDE_FROM_ALL SYSTEM)
add_subdirectory(submodules/glm SYSTEM)

set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
add_subdirectory(submodules/glslang EXCLUDE_FROM_ALL SYSTEM)

set(VK_BOOTSTRAP_VULKAN_HEADER_DIR "${CMAKE_SOURCE_DIR}/submodules/Vulkan-Headers/include" CACHE FILEPATH "" FORCE)
set(VK_BOOTSTRAP_TEST OFF CACHE BOOL "" FORCE)
set(VK_BOOTSTRAP_INSTALL OFF CACHE BOOL "" FORCE)
set(VK_BOOTSTRAP_DISABLE_WARNINGS ON CACHE BOOL "" FORCE)
add_subdirectory(submodules/vk-bootstrap EXCLUDE_FROM_ALL SYSTEM)
set_target_properties(vk-bootstrap PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Cross-platform deterministic Jolt (no FMA, own sin/cos), and no fused a*b+c in our code, so replays match on Windows and Linux.
option(SHARD_DETERMINISTIC_PHYSICS "Build Jolt in cross platform deterministic mode (same simulation on every platform)" ON)
if(SHARD_DETERMINISTIC_PHYSICS)
    set(CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "Cross platform deterministic" FORCE)
    add_compile_definitions(SHARD_DETERMINISTIC_PHYSICS=1)
    if(NOT MSVC)
        add_compile_options(-ffp-contract=off)
    endif()
endif()

# Jolt keeps body positions in double (JPH::RVec3), sizes and velocities stay float. A float is only exact to a mm within ~10 km of the origin.
option(SHARD_DOUBLE_PRECISION_PHYSICS "Build Jolt with double precision positions (large worlds)" ON)
if(SHARD_DOUBLE_PRECISION_PHYSICS)
    set(DOUBLE_PRECISION ON CACHE BOOL "Use double precision math" FORCE)
    add_compile_definitions(SHARD_DOUBLE_PRECISION_PHYSICS=1)
endif()

# Jolt needs RTTI: without it a class deriving from JPH::JobSystemWithBarrier fails to link on Linux (no typeinfo).
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
add_subdirectory(submodules/jolt/Build SYSTEM)

add_subdirectory(src/apps/tools/reflect)

add_subdirectory(src/engine)
add_subdirectory(src/apps/tools/shaderc)
add_subdirectory(src/apps/tools/shardpkg)
add_subdirectory(src/apps/tools/asset_validator)
add_subdirectory(src/apps/game)
add_subdirectory(src/apps/editor)
add_subdirectory(src/apps/player)
add_subdirectory(src/apps/headless)

file(READ ${CMAKE_SOURCE_DIR}/sdk/VERSION SHARD_SDK_VERSION)
string(STRIP "${SHARD_SDK_VERSION}" SHARD_SDK_VERSION)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/sdk/include/ DESTINATION sdk/include COMPONENT sdk)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/sdk/cmake/ DESTINATION sdk/cmake COMPONENT sdk)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/sdk/templates/ DESTINATION sdk/templates COMPONENT sdk)
install(FILES ${CMAKE_SOURCE_DIR}/sdk/VERSION ${CMAKE_SOURCE_DIR}/sdk/README.md DESTINATION sdk COMPONENT sdk)
install(TARGETS ShardPkg RUNTIME DESTINATION sdk/bin COMPONENT sdk)
add_custom_target(ShardSDKArchive
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/sdk_stage
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --component sdk --prefix ${CMAKE_BINARY_DIR}/sdk_stage
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/sdk_stage ${CMAKE_COMMAND} -E tar cf ${CMAKE_BINARY_DIR}/shard-sdk-${SHARD_SDK_VERSION}.zip --format=zip sdk
    COMMENT "Packing the SDK into shard-sdk-${SHARD_SDK_VERSION}.zip"
    VERBATIM)
add_dependencies(ShardSDKArchive ShardPkg)

enable_testing()
add_subdirectory(tests)

shard_enable_warnings(
    ShardCore ShardPlatform ShardAssets ShardWorld ShardPhysics ShardAudio ShardShaderCompiler ShardRHI ShardRenderer ShardEngine
    ShardEditor ShardGame ShardHeadless ShardShaderc ShardPkg ShardAssetValidator ShardTests
)