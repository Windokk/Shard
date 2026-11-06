// Events found by NAME carrying bytes, since C++ events are found by C++ type, which another compiler cannot name.
// Main-thread emits outside systems are delivered at once. System emits are queued and delivered on the main thread at frame end, queues taken in system name order, so delivery order is deterministic.
#ifndef SHARD_EVENTS_H
#define SHARD_EVENTS_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_EVENTS "shard.events"

typedef uint64_t ShardSubscription;

typedef void (SHARD_CALL *ShardEventFn)(void* user, ShardPluginCtx ctx, ShardStr name, const void* payload, uint64_t size);

typedef struct ShardEventsApi {
    uint32_t structSize;
    ShardResult (SHARD_CALL *subscribe)(ShardPluginCtx ctx, ShardStr name, ShardEventFn fn, void* user, ShardSubscription* out);
    ShardResult (SHARD_CALL *subscribe_world)(ShardPluginCtx ctx, int32_t worldId, ShardStr name, ShardEventFn fn, void* user, ShardSubscription* out);
    ShardResult (SHARD_CALL *unsubscribe)(ShardPluginCtx ctx, ShardSubscription subscription);
    ShardResult (SHARD_CALL *emit)(ShardPluginCtx ctx, ShardStr name, const void* payload, uint64_t size);
    ShardResult (SHARD_CALL *emit_world)(ShardPluginCtx ctx, int32_t worldId, ShardStr name, const void* payload, uint64_t size);
} ShardEventsApi;

#ifdef __cplusplus
}
#endif

#endif
