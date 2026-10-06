# Layers, bottom to top (the dependency rules are checked by scripts/check_deps.py):
#   ShardCore     <- nothing
#   ShardPlatform <- ShardCore
#   Engine        <- everything else (links the two above)
file(GLOB_RECURSE CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp)
file(GLOB_RECURSE PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/platform/*.cpp)
file(GLOB_RECURSE ENGINE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/renderer/rhi/backends/glad/src/gl.c
  ${CMAKE_CURRENT_SOURCE_DIR}/renderer/rhi/backends/glad/src/vulkan.c
  ${CMAKE_SOURCE_DIR}/submodules/ufbx/ufbx.c
)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/engine/(core|platform)/")

if(WIN32)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
endif()

add_library(ShardCore STATIC ${CORE_SOURCES})
# BUILD_ENGINE selects the engine-side implementation in logger.hpp, like for Engine.
target_compile_definitions(ShardCore PRIVATE BUILD_ENGINE)
target_include_directories(ShardCore PUBLIC
    "${CMAKE_SOURCE_DIR}/src/"
    "${CMAKE_SOURCE_DIR}/submodules/glm/"
)
if(WIN32)
  target_link_libraries(ShardCore PRIVATE pdh)  # GPU counters of the profiler
endif()

add_library(ShardPlatform STATIC ${PLATFORM_SOURCES})
target_compile_definitions(ShardPlatform PRIVATE BUILD_ENGINE)
# Only the platform layer sees the window system's headers (the apps add their own include path).
target_include_directories(ShardPlatform PUBLIC "${CMAKE_SOURCE_DIR}/submodules/glfw/include/")
target_link_libraries(ShardPlatform PUBLIC ShardCore)

add_library(Engine STATIC ${ENGINE_SOURCES})
target_link_libraries(Engine PUBLIC ShardPlatform ShardCore)

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
target_include_directories(Engine PUBLIC
    ${COMMON_INCLUDE_DIRS}
    ${FMOD_API_DIR}/inc
)

# Link directories based on platform (Windows or Unix)
set(FREETYPE_LIB_DIR "${CMAKE_SOURCE_DIR}/build/submodules/freetype")

if(WIN32)
  set(JOLT_LIB_DIR "${CMAKE_SOURCE_DIR}/build/submodules/jolt/Build")
  target_link_directories(Engine PUBLIC
    ${FREETYPE_LIB_DIR}
    ${JOLT_LIB_DIR}
    ${FMOD_API_DIR}/lib/x64
  )
elseif(UNIX)
  set(JOLT_LIB_DIR "${CMAKE_SOURCE_DIR}/build/submodules/jolt/Build/Linux_Debug")
  target_link_directories(Engine PUBLIC
    ${FREETYPE_LIB_DIR}
    ${JOLT_LIB_DIR}
    ${FMOD_API_DIR}/lib/x86_64
  )
endif()

# Define engine-specific compile definitions
target_compile_definitions(Engine PRIVATE BUILD_ENGINE)

# Jolt is PUBLIC, not PRIVATE: engine headers (physics_manager.hpp, physics_body.hpp, ...) expose
# Jolt types, so anything including them has to be compiled with Jolt's own PUBLIC definitions
# (JPH_DEBUG_RENDERER, JPH_PROFILE_ENABLED, ...). Those defines change the layout of Jolt classes,
# so a target that gets the headers without them disagrees with the compiled library about where
# members live - which corrupts memory at runtime rather than failing to build.

# Link the necessary libraries based on the platform
if(WIN32)
  target_link_libraries(Engine PRIVATE freetype fmod -lz opengl32 gdi32 user32 shell32 advapi32)
  target_link_libraries(Engine PUBLIC Jolt Threads::Threads)
elseif(UNIX)
  target_link_libraries(Engine PRIVATE freetype fmod -lz)
  target_link_libraries(Engine PUBLIC Jolt)
endif()

# Executables that link Engine need fmod.dll next to them (otherwise they exit with 0xc0000135 on
# Windows, which also breaks gtest_discover_tests at build time). Call this after add_executable.
function(shard_copy_fmod target)
  if(WIN32 AND EXISTS "${FMOD_API_DIR}/lib/x64/fmod.dll")
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${FMOD_API_DIR}/lib/x64/fmod.dll" "$<TARGET_FILE_DIR:${target}>")
  endif()
endfunction()