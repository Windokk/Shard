/* Interface "shard.state" 1.0 : what a plugin keeps across a hot reload. on_prepare_reload (shard_host.h) writes values into the ShardState
 * it is handed, the new binary reads them back in on_restore. Values are JSON text (an object, a number, a string in quotes...) : the
 * format does not depend on the memory layout, so the new binary may have changed its structs. The handle is valid during that callback only.
 * thread : main */
#ifndef SHARD_STATE_H
#define SHARD_STATE_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_STATE "shard.state"

typedef struct ShardStateApi {
    uint32_t structSize;
    /* on_prepare_reload. SHARD_E_INVALID_ARG : `json` does not parse. */
    ShardResult (SHARD_CALL *set)(ShardState state, ShardStr key, ShardStr json);
    /* on_restore. The value of `key` as JSON text (compact) ; copies at most `capacity` bytes (no zero terminator) and stores the full length
     * in *size ; `buffer` may be NULL to ask for the length. SHARD_E_NOT_FOUND : no such key. */
    ShardResult (SHARD_CALL *get)(ShardState state, ShardStr key, char* buffer, uint64_t capacity, uint64_t* size);
    int32_t     (SHARD_CALL *has)(ShardState state, ShardStr key);
} ShardStateApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_STATE_H */
