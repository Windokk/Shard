#ifndef SHARD_CVARS_H
#define SHARD_CVARS_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_CVARS "shard.cvars"

#define SHARD_CVAR_READONLY 1u

typedef uint64_t ShardCVar;

typedef struct ShardCVarsApi {
    uint32_t structSize;

    ShardResult (SHARD_CALL *create_bool)(ShardPluginCtx ctx, ShardStr name, int32_t value, ShardStr help, uint32_t flags, ShardCVar* out);
    ShardResult (SHARD_CALL *create_int)(ShardPluginCtx ctx, ShardStr name, int32_t value, ShardStr help, uint32_t flags, int32_t min, int32_t max, ShardCVar* out);
    ShardResult (SHARD_CALL *create_float)(ShardPluginCtx ctx, ShardStr name, float value, ShardStr help, uint32_t flags, float min, float max, ShardCVar* out);
    ShardResult (SHARD_CALL *create_string)(ShardPluginCtx ctx, ShardStr name, ShardStr value, ShardStr help, uint32_t flags, ShardCVar* out);

    ShardResult (SHARD_CALL *get_bool)(ShardPluginCtx ctx, ShardCVar cvar, int32_t* out);
    ShardResult (SHARD_CALL *get_int)(ShardPluginCtx ctx, ShardCVar cvar, int32_t* out);
    ShardResult (SHARD_CALL *get_float)(ShardPluginCtx ctx, ShardCVar cvar, float* out);
    // Copies at most `capacity` bytes (no terminator), stores the full length in *size. NULL buffer asks for the length.
    ShardResult (SHARD_CALL *get_string)(ShardPluginCtx ctx, ShardCVar cvar, char* buffer, uint64_t capacity, uint64_t* size);

    ShardResult (SHARD_CALL *set_bool)(ShardPluginCtx ctx, ShardCVar cvar, int32_t value);
    ShardResult (SHARD_CALL *set_int)(ShardPluginCtx ctx, ShardCVar cvar, int32_t value);
    ShardResult (SHARD_CALL *set_float)(ShardPluginCtx ctx, ShardCVar cvar, float value);
    ShardResult (SHARD_CALL *set_string)(ShardPluginCtx ctx, ShardCVar cvar, ShardStr value);
} ShardCVarsApi;

#ifdef __cplusplus
}
#endif

#endif
