// State kept across a hot reload as JSON text, so the new binary may have changed its struct layouts. Handle valid only during the callback.
#ifndef SHARD_STATE_H
#define SHARD_STATE_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_STATE "shard.state"

typedef struct ShardStateApi {
    uint32_t structSize;
    ShardResult (SHARD_CALL *set)(ShardState state, ShardStr key, ShardStr json);
    ShardResult (SHARD_CALL *get)(ShardState state, ShardStr key, char* buffer, uint64_t capacity, uint64_t* size);
    int32_t     (SHARD_CALL *has)(ShardState state, ShardStr key);
} ShardStateApi;

#ifdef __cplusplus
}
#endif

#endif
