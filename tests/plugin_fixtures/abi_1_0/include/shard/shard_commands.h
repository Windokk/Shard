#ifndef SHARD_COMMANDS_H
#define SHARD_COMMANDS_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_COMMANDS "shard.commands"

typedef ShardResult (SHARD_CALL *ShardCommandFn)(void* user, ShardPluginCtx ctx, const ShardStr* args, uint32_t argCount, ShardStr* output);

typedef struct ShardCommandsApi {
    uint32_t structSize;
    // Names are used as given, so prefix your own ("weather.set"). A name another plugin or the engine uses is replaced while this plugin lives.
    ShardResult (SHARD_CALL *add)(ShardPluginCtx ctx, ShardStr name, ShardStr help, ShardCommandFn run, void* user);
} ShardCommandsApi;

#ifdef __cplusplus
}
#endif

#endif
