/* Interface "shard.commands" 1.1 : console commands. The command is removed with the plugin (disable or unload).
 * 1.1 added `execute`: running a command of the console (the engine's, a plugin's) from code -- what a UI button, a graph node or a script does. */
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

    /* ---- 1.1 ---- */
    /* Runs a console line ("world.load arena", "render.thread 0") as if it was typed. The command's output is copied as a "text out" : at most
     * `capacity` bytes into `output` (may be NULL), the full length in *size (may be NULL). SHARD_OK if the command succeeded, SHARD_E_NOT_FOUND
     * if the line names no command, SHARD_E_INVALID_ARG if it failed (the output says why). A command that changes what the game simulates is
     * the caller's to run from the simulation thread. thread : main */
    ShardResult (SHARD_CALL *execute)(ShardPluginCtx ctx, ShardStr line, char* output, uint64_t capacity, uint64_t* size);
} ShardCommandsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_COMMANDS_H */
