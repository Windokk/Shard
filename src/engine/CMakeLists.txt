# Layers, bottom to top (the dependency rules are checked by scripts/check_deps.py):
#   ShardCore     <- nothing
#   ShardPlatform <- ShardCore, SDL3 : the PAL (windowing, events, devices, filesystem, threads, time, process, network, hardware...)
#   ShardAssets   <- ShardPlatform, ShardCore
#   ShardWorld    <- ShardAssets (and below)
#   ShardPhysics  <- ShardWorld (and below), Jolt
#   ShardAudio    <- ShardWorld (and below), FMOD
#   ShardRHI      <- ShardWorld (and below) : the graphics API, nothing of the renderer above it
#   ShardRenderer <- ShardRHI, ShardWorld (and below) : features, components and the Renderer
#   ShardEngine   <- every layer above : the composition root (EngineInstance, game module loading)
#   Engine        <- INTERFACE target grouping all of the above, what the apps and the tests link
file(GLOB_RECURSE CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp)
file(GLOB_RECURSE PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/platform/*.cpp)
file(GLOB_RECURSE ASSETS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/assets/*.cpp)
file(GLOB_RECURSE WORLD_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/world/*.cpp)
file(GLOB_RECURSE PHYSICS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/physics/*.cpp)
file(GLOB_RECURSE AUDIO_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/audio/*.cpp)
file(GLOB_RECURSE RHI_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/renderer/rhi/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/renderer/rhi/backends/glad/src/gl.c
  ${CMAKE_CURRENT_SOURCE_DIR}/renderer/rhi/backends/glad/src/vulkan.c
  ${CMAKE_SOURCE_DIR}/submodules/ufbx/ufbx.c
)
file(GLOB_RECURSE RENDERER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/renderer/*.cpp)
list(FILTER RENDERER_SOURCES EXCLUDE REGEX "/renderer/rhi/")
file(GLOB_RECURSE ENGINE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/engine/(core|platform|assets|world|physics|audio|renderer)/")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(ShardCore STATIC ${CORE_SOURCES})
# BUILD_ENGINE selects the engine-side implementation in logger.hpp, like for ShardEngine.
target_compile_definitions(ShardCore PRIVATE BUILD_ENGINE)
target_include_directories(ShardCore PUBLIC
    "${CMAKE_SOURCE_DIR}/src/"
    "${CMAKE_SOURCE_DIR}/submodules/glm/"
)

add_library(ShardPlatform STATIC ${PLATFORM_SOURCES})
target_compile_definitions(ShardPlatform PRIVATE BUILD_ENGINE)
# SDL3 is PUBLIC : the apps that own a window (editor, player) need its headers, the engine layers only see the platform layer's own API
target_link_libraries(ShardPlatform PUBLIC ShardCore SDL3::SDL3)
if(WIN32)
  # pdh/psapi : hardware counters, ws2_32 : sockets
  target_link_libraries(ShardPlatform PRIVATE pdh psapi ws2_32)
else()
  target_link_libraries(ShardPlatform PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(ShardPlatform PRIVATE rt)   # shm_open on older glibc

    # io_uring backend of the async IO (platform/filesystem) : optional, the thread pool backend is used without it
    # (and when the kernel refuses to create a ring at run time)
    find_path(URING_INCLUDE_DIR liburing.h)
    find_library(URING_LIBRARY uring)
    if(URING_INCLUDE_DIR AND URING_LIBRARY)
      message(STATUS "liburing found : async IO uses io_uring")
      target_compile_definitions(ShardPlatform PRIVATE SHARD_HAS_IO_URING)
      target_include_directories(ShardPlatform PRIVATE ${URING_INCLUDE_DIR})
      target_link_libraries(ShardPlatform PRIVATE ${URING_LIBRARY})
    else()
      message(STATUS "liburing not found : async IO uses its thread pool backend (apt install liburing-dev)")
    endif()
  endif()
endif()

add_library(ShardAssets STATIC ${ASSETS_SOURCES})
target_compile_definitions(ShardAssets PRIVATE BUILD_ENGINE)
target_include_directories(ShardAssets PUBLIC
    "${CMAKE_SOURCE_DIR}/submodules/"
    "${CMAKE_SOURCE_DIR}/submodules/json/single_include/"
)
target_link_libraries(ShardAssets PUBLIC ShardPlatform ShardCore)

add_library(ShardWorld STATIC ${WORLD_SOURCES})
target_compile_definitions(ShardWorld PRIVATE BUILD_ENGINE)
target_include_directories(ShardWorld PUBLIC
    "${CMAKE_SOURCE_DIR}/src/"
    "${CMAKE_SOURCE_DIR}/submodules/glm/"
)
target_link_libraries(ShardWorld PUBLIC ShardAssets ShardPlatform ShardCore)

add_library(ShardEngine STATIC ${ENGINE_SOURCES})
add_library(ShardPhysics STATIC ${PHYSICS_SOURCES})
target_compile_definitions(ShardPhysics PRIVATE BUILD_ENGINE)
target_link_libraries(ShardPhysics PUBLIC ShardWorld ShardAssets ShardPlatform ShardCore)

add_library(ShardAudio STATIC ${AUDIO_SOURCES})
target_compile_definitions(ShardAudio PRIVATE BUILD_ENGINE)
target_link_libraries(ShardAudio PUBLIC ShardWorld ShardAssets ShardPlatform ShardCore)

add_library(ShardRHI STATIC ${RHI_SOURCES})
target_compile_definitions(ShardRHI PRIVATE BUILD_ENGINE)
target_link_libraries(ShardRHI PUBLIC ShardWorld ShardAssets ShardPlatform ShardCore)

add_library(ShardRenderer STATIC ${RENDERER_SOURCES})
target_compile_definitions(ShardRenderer PRIVATE BUILD_ENGINE)
target_link_libraries(ShardRenderer PUBLIC ShardRHI ShardWorld ShardAssets ShardPlatform ShardCore)

target_link_libraries(ShardEngine PUBLIC ShardRenderer ShardRHI ShardPhysics ShardAudio ShardWorld ShardAssets ShardPlatform ShardCore)

set(FMOD_API_DIR "" CACHE PATH "Path to fmod api")

set(COMMON_INCLUDE_DIRS 
    "${CMAKE_SOURCE_DIR}/src/"
    "${CMAKE_SOURCE_DIR}/submodules/"
    "${CMAKE_SOURCE_DIR}/submodules/json/single_include/"
    "${CMAKE_SOURCE_DIR}/submodules/jolt/"
    "${CMAKE_SOURCE_DIR}/submodules/glm/"
    "${CMAKE_SOURCE_DIR}/submodules/freetype/include/"
)

# Set the include directories
target_include_directories(ShardEngine PUBLIC
    ${COMMON_INCLUDE_DIRS}
    ${FMOD_API_DIR}/inc
)

# Link directories based on platform (Windows or Unix)
set(FREETYPE_LIB_DIR "${CMAKE_SOURCE_DIR}/build/submodules/freetype")

if(WIN32)
  set(JOLT_LIB_DIR "${CMAKE_SOURCE_DIR}/build/submodules/jolt/Build")
  target_link_directories(ShardEngine PUBLIC
    ${FREETYPE_LIB_DIR}
    ${JOLT_LIB_DIR}
    ${FMOD_API_DIR}/lib/x64
  )
elseif(UNIX)
  set(JOLT_LIB_DIR "${CMAKE_SOURCE_DIR}/build/submodules/jolt/Build/Linux_Debug")
  target_link_directories(ShardEngine PUBLIC
    ${FREETYPE_LIB_DIR}
    ${JOLT_LIB_DIR}
    ${FMOD_API_DIR}/lib/x86_64
  )
endif()

target_include_directories(ShardPhysics PUBLIC ${COMMON_INCLUDE_DIRS})
target_include_directories(ShardRHI PUBLIC ${COMMON_INCLUDE_DIRS})
target_include_directories(ShardRenderer PUBLIC ${COMMON_INCLUDE_DIRS})
target_include_directories(ShardAudio PUBLIC ${COMMON_INCLUDE_DIRS} ${FMOD_API_DIR}/inc)

# Define engine-specific compile definitions
target_compile_definitions(ShardEngine PRIVATE BUILD_ENGINE)

# Jolt is PUBLIC, not PRIVATE: engine headers (physics_manager.hpp, physics_body.hpp, ...) expose
# Jolt types, so anything including them has to be compiled with Jolt's own PUBLIC definitions
# (JPH_DEBUG_RENDERER, JPH_PROFILE_ENABLED, ...). Those defines change the layout of Jolt classes,
# so a target that gets the headers without them disagrees with the compiled library about where
# members live - which corrupts memory at runtime rather than failing to build.

# The graphics API is linked here, the rest of the renderer only talks to it through the rhi
if(WIN32)
  target_link_libraries(ShardRHI PRIVATE opengl32 gdi32 user32 shell32 advapi32)
endif()
target_link_libraries(ShardRenderer PRIVATE freetype)
target_link_directories(ShardRenderer PUBLIC ${FREETYPE_LIB_DIR})

# Jolt is PUBLIC for the same reason on its own target, FMOD is only needed to link
target_link_libraries(ShardPhysics PUBLIC Jolt)
target_link_libraries(ShardAudio PRIVATE fmod)
if(WIN32)
  target_link_directories(ShardAudio PUBLIC ${FMOD_API_DIR}/lib/x64)
  target_link_libraries(ShardPhysics PUBLIC Threads::Threads)
elseif(UNIX)
  target_link_directories(ShardAudio PUBLIC ${FMOD_API_DIR}/lib/x86_64)
endif()

# Link the necessary libraries based on the platform
if(WIN32)
  target_link_libraries(ShardEngine PRIVATE freetype fmod -lz opengl32 gdi32 user32 shell32 advapi32)
  target_link_libraries(ShardEngine PUBLIC Jolt Threads::Threads)
elseif(UNIX)
  target_link_libraries(ShardEngine PRIVATE freetype fmod -lz)
  target_link_libraries(ShardEngine PUBLIC Jolt)
endif()

# What an executable links to get the whole engine : the layers above, nothing of its own
add_library(Engine INTERFACE)
target_link_libraries(Engine INTERFACE ShardEngine)

# Executables that link Engine need fmod.dll next to them (otherwise they exit with 0xc0000135 on
# Windows, which also breaks gtest_discover_tests at build time). Call this after add_executable.
function(shard_copy_fmod target)
  if(WIN32 AND EXISTS "${FMOD_API_DIR}/lib/x64/fmod.dll")
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${FMOD_API_DIR}/lib/x64/fmod.dll" "$<TARGET_FILE_DIR:${target}>")
  endif()
endfunction()