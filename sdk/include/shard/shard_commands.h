/* Interface "shard.commands" 1.0 : console commands. The command is removed with the plugin (disable or unload). */
#ifndef SHARD_COMMANDS_H
#define SHARD_COMMANDS_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_COMMANDS "shard.commands"

/* Runs the command. `args` are the words after the command name (`argCount` of them, valid for the call). The callback may point
 * `*output` to a text to show the user (it is copied before the callback's caller returns : a static buffer or the plugin's own will do).
 * Return SHARD_OK for success ; anything else is shown as a failure, with the output as the message. thread : main */
typedef ShardResult (SHARD_CALL *ShardCommandFn)(void* user, ShardPluginCtx ctx, const ShardStr* args, uint32_t argCount, ShardStr* output);

typedef struct ShardCommandsApi {
    uint32_t structSize;
    /* The name is used as given : prefix it with your own ("weather.set"). SHARD_E_ALREADY_EXISTS if this plugin already added that name
     * (case does not matter). A name another plugin or the engine already uses is replaced for as long as this plugin lives : the prefix is
     * what keeps commands apart. thread : main */
    ShardResult (SHARD_CALL *add)(ShardPluginCtx ctx, ShardStr name, ShardStr help, ShardCommandFn run, void* user);
} ShardCommandsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_COMMANDS_H */
