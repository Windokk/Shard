/* Interface "shard.events" 1.0 : events found by NAME, carrying bytes.
 *
 * The engine's C++ events are found by their C++ type, which a binary built by another compiler cannot name : the C ABI has a channel of
 * its own, where an event is a name ("com.acme.weather.RainStarted") and a payload of bytes whose layout the sender documents. C++ code
 * reaches the same channel (EventRegistrar::SubscribeNamed / EmitNamed). Subscriptions end with the plugin (disable or unload).
 *
 * Delivery : an event emitted on the main thread outside of the systems is delivered at once, to the subscribers in the order they
 * subscribed. One emitted from a system (any thread) is queued and delivered on the main thread at the end of the frame, the queues of the
 * systems taken in the order of their names : the order does not depend on which system finished first. */
#ifndef SHARD_EVENTS_H
#define SHARD_EVENTS_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_EVENTS "shard.events"

typedef uint64_t ShardSubscription;     /* 0 is never valid */

/* `name` and `payload` are valid for the call. thread : main */
typedef void (SHARD_CALL *ShardEventFn)(void* user, ShardPluginCtx ctx, ShardStr name, const void* payload, uint64_t size);

typedef struct ShardEventsApi {
    uint32_t structSize;
    /* Events sent globally under `name`. thread : main */
    ShardResult (SHARD_CALL *subscribe)(ShardPluginCtx ctx, ShardStr name, ShardEventFn fn, void* user, ShardSubscription* out);
    /* Events sent to one world (its asset id) under `name`. thread : main */
    ShardResult (SHARD_CALL *subscribe_world)(ShardPluginCtx ctx, int32_t worldId, ShardStr name, ShardEventFn fn, void* user, ShardSubscription* out);
    /* thread : main. SHARD_E_NOT_FOUND : not one of this plugin's live subscriptions. */
    ShardResult (SHARD_CALL *unsubscribe)(ShardPluginCtx ctx, ShardSubscription subscription);
    /* The payload is copied. thread : any (see the delivery rules above). SHARD_E_UNSUPPORTED : the engine has no dispatcher (an offline tool). */
    ShardResult (SHARD_CALL *emit)(ShardPluginCtx ctx, ShardStr name, const void* payload, uint64_t size);
    ShardResult (SHARD_CALL *emit_world)(ShardPluginCtx ctx, int32_t worldId, ShardStr name, const void* payload, uint64_t size);
} ShardEventsApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_EVENTS_H */
