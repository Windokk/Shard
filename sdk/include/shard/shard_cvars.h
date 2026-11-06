/* Interface "shard.cvars" 1.0 : variables the plugin owns, readable from the console and the config files like the engine's own.
 * A variable is named "<plugin id>.<name>" by the host. It lives until the plugin is disabled or unloaded ; after that its handle is stale
 * (SHARD_E_STALE_HANDLE). The plugin changes its own variables with set_* (that bypasses the config layers and the read-only flag, as the
 * owner) ; the console and the config files change them from outside. Numeric values are clamped to [min, max]. */
#ifndef SHARD_CVARS_H
#define SHARD_CVARS_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_CVARS "shard.cvars"

#define SHARD_CVAR_READONLY 1u      /* the console and the config files cannot change it */

typedef uint64_t ShardCVar;         /* generation << 32 | slot ; 0 is never valid */

typedef struct ShardCVarsApi {
    uint32_t structSize;

    /* thread : main. SHARD_E_ALREADY_EXISTS if the name is taken. */
    ShardResult (SHARD_CALL *create_bool)(ShardPluginCtx ctx, ShardStr name, int32_t value, ShardStr help, uint32_t flags, ShardCVar* out);
    ShardResult (SHARD_CALL *create_int)(ShardPluginCtx ctx, ShardStr name, int32_t value, ShardStr help, uint32_t flags, int32_t min, int32_t max, ShardCVar* out);
    ShardResult (SHARD_CALL *create_float)(ShardPluginCtx ctx, ShardStr name, float value, ShardStr help, uint32_t flags, float min, float max, ShardCVar* out);
    ShardResult (SHARD_CALL *create_string)(ShardPluginCtx ctx, ShardStr name, ShardStr value, ShardStr help, uint32_t flags, ShardCVar* out);

    /* thread : any. SHARD_E_INVALID_ARG if the variable is of another type. */
    ShardResult (SHARD_CALL *get_bool)(ShardPluginCtx ctx, ShardCVar cvar, int32_t* out);
    ShardResult (SHARD_CALL *get_int)(ShardPluginCtx ctx, ShardCVar cvar, int32_t* out);
    ShardResult (SHARD_CALL *get_float)(ShardPluginCtx ctx, ShardCVar cvar, float* out);
    /* Copies at most `capacity` bytes (no zero terminator) into `buffer` and stores the full length in *size : a value longer than the
     * buffer is cut, and the caller can call again with a bigger one. `buffer` may be NULL to ask for the length. */
    ShardResult (SHARD_CALL *get_string)(ShardPluginCtx ctx, ShardCVar cvar, char* buffer, uint64_t capacity, uint64_t* size);

    /* thread : main */
    ShardResult (SHARD_CALL *set_bool)(ShardPluginCtx ctx, ShardCVar cvar, int32_t value);
    ShardResult (SHARD_CALL *set_int)(ShardPluginCtx ctx, ShardCVar cvar, int32_t value);
    ShardResult (SHARD_CALL *set_float)(ShardPluginCtx ctx, ShardCVar cvar, float value);
    ShardResult (SHARD_CALL *set_string)(ShardPluginCtx ctx, ShardCVar cvar, ShardStr value);
} ShardCVarsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_CVARS_H */
