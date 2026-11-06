# Layers, bottom to top (dependency rules checked by scripts/check_deps.py): Core, Platform, Assets, World, Physics/Audio/RHI, Renderer, Engine.
file(GLOB_RECURSE CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp)
file(GLOB_RECURSE PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/platform/*.cpp)
file(GLOB_RECURSE ASSETS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/assets/*.cpp)
file(GLOB_RECURSE WORLD_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/world/*.cpp)
file(GLOB_RECURSE PHYSICS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/physics/*.cpp)
file(GLOB_RECURSE AUDIO_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/audio/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/audio/*.c)
file(GLOB_RECURSE RHI_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/renderer/rhi/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/renderer/rhi/backends/glad/src/gl.c
  ${CMAKE_SOURCE_DIR}/submodules/ufbx/ufbx.c
  ${CMAKE_SOURCE_DIR}/submodules/volk/volk.c
)
file(GLOB SHADER_COMPILER_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/renderer/rhi/shader_compiler/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/renderer/rhi/shader/shader_binary.cpp
)
list(FILTER RHI_SOURCES EXCLUDE REGEX "/renderer/rhi/shader_compiler/|/renderer/rhi/shader/shader_binary[.]cpp")
file(GLOB_RECURSE RENDERER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/renderer/*.cpp)
list(FILTER RENDERER_SOURCES EXCLUDE REGEX "/renderer/rhi/")
file(GLOB_RECURSE ENGINE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/engine/(core|platform|assets|world|physics|audio|renderer)/")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(ShardCore STATIC ${CORE_SOURCES})
target_compile_definitions(ShardCore PRIVATE BUILD_ENGINE)
target_include_directories(ShardCore PUBLIC
    "${CMAKE_SOURCE_DIR}/src/"
    "${CMAKE_SOURCE_DIR}/submodules/glm/"
)

add_library(ShardPlatform STATIC ${PLATFORM_SOURCES})
target_compile_definitions(ShardPlatform PRIVATE BUILD_ENGINE)
target_link_libraries(ShardPlatform PUBLIC ShardCore SDL3::SDL3)
if(WIN32)
  target_link_libraries(ShardPlatform PRIVATE pdh psapi ws2_32)
else()
  target_link_libraries(ShardPlatform PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(ShardPlatform PRIVATE rt)   # shm_open on older glibc

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

add_library(ShardShaderCompiler STATIC ${SHADER_COMPILER_SOURCES} ${CMAKE_SOURCE_DIR}/submodules/SPIRV-Reflect/spirv_reflect.c)
target_compile_definitions(ShardShaderCompiler PRIVATE BUILD_ENGINE)
target_include_directories(ShardShaderCompiler PUBLIC
    "${CMAKE_SOURCE_DIR}/src/"
    "${CMAKE_SOURCE_DIR}/sdk/include/"
    "${CMAKE_SOURCE_DIR}/submodules/"
    "${CMAKE_SOURCE_DIR}/submodules/json/single_include/"
    "${CMAKE_SOURCE_DIR}/submodules/glm/"
    "${CMAKE_SOURCE_DIR}/submodules/SPIRV-Reflect/"
)
target_link_libraries(ShardShaderCompiler PUBLIC glslang SPIRV glslang-default-resource-limits ShardAssets ShardCore)

add_library(ShardRHI STATIC ${RHI_SOURCES})
target_compile_definitions(ShardRHI PRIVATE BUILD_ENGINE)
target_link_libraries(ShardRHI PUBLIC ShardShaderCompiler ShardWorld ShardAssets ShardPlatform ShardCore)
target_link_libraries(ShardRHI PUBLIC vk-bootstrap::vk-bootstrap)
target_include_directories(ShardRHI PUBLIC
    "${CMAKE_SOURCE_DIR}/submodules/Vulkan-Headers/include"
    "${CMAKE_SOURCE_DIR}/submodules/volk"
    "${CMAKE_SOURCE_DIR}/submodules/VulkanMemoryAllocator/include"
    "${CMAKE_SOURCE_DIR}/submodules/vk-bootstrap/src"
)
# volk declares the entry points itself, so the Vulkan headers must not (see volk.h)
target_compile_definitions(ShardRHI PUBLIC VK_NO_PROTOTYPES)

add_library(ShardRenderer STATIC ${RENDERER_SOURCES})
target_compile_definitions(ShardRenderer PRIVATE BUILD_ENGINE)
target_link_libraries(ShardRenderer PUBLIC ShardRHI ShardWorld ShardAssets ShardPlatform ShardCore)

target_link_libraries(ShardEngine PUBLIC ShardRenderer ShardRHI ShardPhysics ShardAudio ShardWorld ShardAssets ShardPlatform ShardCore)
target_link_libraries(ShardEngine PRIVATE ShardWasmtime)

set(COMMON_INCLUDE_DIRS 
    "${CMAKE_SOURCE_DIR}/src/"
    "${CMAKE_SOURCE_DIR}/submodules/"
    "${CMAKE_SOURCE_DIR}/submodules/json/single_include/"
    "${CMAKE_SOURCE_DIR}/submodules/jolt/"
    "${CMAKE_SOURCE_DIR}/submodules/glm/"
    "${CMAKE_SOURCE_DIR}/submodules/freetype/include/"
)

target_include_directories(ShardEngine PUBLIC
    ${COMMON_INCLUDE_DIRS}
)

set(FREETYPE_LIB_DIR "${CMAKE_SOURCE_DIR}/build/submodules/freetype")

if(WIN32)
  set(JOLT_LIB_DIR "${CMAKE_SOURCE_DIR}/build/submodules/jolt/Build")
  target_link_directories(ShardEngine PUBLIC
    ${FREETYPE_LIB_DIR}
    ${JOLT_LIB_DIR}
  )
elseif(UNIX)
  set(JOLT_LIB_DIR "${CMAKE_SOURCE_DIR}/build/submodules/jolt/Build/Linux_Debug")
  target_link_directories(ShardEngine PUBLIC
    ${FREETYPE_LIB_DIR}
    ${JOLT_LIB_DIR}
  )
endif()

target_include_directories(ShardPhysics PUBLIC ${COMMON_INCLUDE_DIRS})
target_include_directories(ShardRHI PUBLIC ${COMMON_INCLUDE_DIRS})
target_include_directories(ShardRenderer PUBLIC ${COMMON_INCLUDE_DIRS})
target_include_directories(ShardAudio PUBLIC ${COMMON_INCLUDE_DIRS})

target_compile_definitions(ShardEngine PRIVATE BUILD_ENGINE)

# Jolt is PUBLIC: engine headers expose Jolt types, so every includer needs Jolt's PUBLIC defines (JPH_DEBUG_RENDERER, JPH_PROFILE_ENABLED...).
# They change class layouts, and a mismatch corrupts memory at runtime instead of failing to build.

if(WIN32)
  target_link_libraries(ShardRHI PRIVATE opengl32 gdi32 user32 shell32 advapi32)
endif()
target_link_libraries(ShardRenderer PRIVATE freetype)
target_link_directories(ShardRenderer PUBLIC ${FREETYPE_LIB_DIR})

target_link_libraries(ShardPhysics PUBLIC Jolt)
if(UNIX)
  target_link_libraries(ShardAudio PRIVATE ${CMAKE_DL_LIBS} Threads::Threads m)
endif()
if(WIN32)
  target_link_libraries(ShardPhysics PUBLIC Threads::Threads)
endif()

if(WIN32)
  target_link_libraries(ShardEngine PRIVATE freetype -lz opengl32 gdi32 user32 shell32 advapi32)
  target_link_libraries(ShardEngine PUBLIC Jolt Threads::Threads)
elseif(UNIX)
  target_link_libraries(ShardEngine PRIVATE freetype -lz)
  target_link_libraries(ShardEngine PUBLIC Jolt)
endif()

add_library(Engine INTERFACE)
target_link_libraries(Engine INTERFACE ShardEngine)
