# find_package(ShardSDK 1.0) : the Shard SDK, what a plugin built outside the engine's repository needs.
#
#   Shard::SDK              an INTERFACE target : the headers of the C ABI (shard/shard.h...) and of the C++ SDK (shard/cpp/plugin.hpp...)
#   ShardSDK_INCLUDE_DIRS   the same folder
#   ShardSDK_VERSION        the SDK's version = the C ABI's (major.minor of SHARD_ABI_MAJOR / SHARD_ABI_MINOR)
#   shard_sdk_add_plugin()  builds a plugin and lays it out the way the engine finds it (see ShardSDKPlugin.cmake)
#
# The SDK is the folder above this file : cmake --install of the engine puts it in <prefix>/sdk (--component sdk), and the repository's own
# sdk/ folder is one too. Point CMake at it with -DCMAKE_PREFIX_PATH=<that folder> or -DShardSDK_DIR=<that folder>/cmake.

get_filename_component(SHARD_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SHARD_SDK_INCLUDE_DIR "${SHARD_SDK_DIR}/include")
set(ShardSDK_INCLUDE_DIRS "${SHARD_SDK_INCLUDE_DIR}")

if(NOT EXISTS "${SHARD_SDK_INCLUDE_DIR}/shard/shard.h")
    set(ShardSDK_FOUND FALSE)
    set(ShardSDK_NOT_FOUND_MESSAGE "no shard/shard.h in ${SHARD_SDK_INCLUDE_DIR}")
    return()
endif()

if(NOT TARGET Shard::SDK)
    add_library(Shard::SDK INTERFACE IMPORTED)
    set_target_properties(Shard::SDK PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${SHARD_SDK_INCLUDE_DIR}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/ShardSDKPlugin.cmake")
