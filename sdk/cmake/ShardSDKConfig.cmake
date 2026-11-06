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
