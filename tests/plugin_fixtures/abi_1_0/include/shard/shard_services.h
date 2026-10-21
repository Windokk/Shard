/* Interface "shard.services" 1.0 : interfaces plugins offer each other, by name and version, without including each other's code.
 *
 * A service of the C ABI is a table of function pointers that starts with `uint32_t structSize` (and, like the ABI's own tables, only grows
 * at its end), plus a `self` the functions get back. The provider publishes the header of its table ; the users include it. The engine
 * never calls the table : it only hands it out.
 *
 * A user holds a ShardServiceRef, not the table : resolve it before each use. When the provider is disabled or unloaded, the ref resolves
 * to nothing (SHARD_E_NOT_FOUND) instead of to a table in a library that is gone. Refs are released with the plugin that took them. */
#ifndef SHARD_SERVICES_H
#define SHARD_SERVICES_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_SERVICES "shard.services"

typedef uint64_t ShardServiceRef;   /* 0 is never valid */

typedef struct ShardService {
    uint32_t    structSize;
    uint32_t    major, minor;       /* the version the provider offers */
    const void* table;              /* the provider's table (its first field is its structSize) */
    void*       self;
} ShardService;

typedef struct ShardServicesApi {
    uint32_t structSize;
    /* Offers `table` and `self` as service `name` (copied ; `table` must stay valid until the plugin is disabled). thread : main.
     * SHARD_E_ALREADY_EXISTS : another plugin provides that name. */
    ShardResult (SHARD_CALL *provide)(ShardPluginCtx ctx, ShardStr name, uint32_t major, uint32_t minor, const void* table, void* self);
    /* A ref on the service `name` if its version is in `range` ("^1.2", ">=1", "*"). thread : main.
     * SHARD_E_NOT_FOUND : nobody provides it, or not in that range ; SHARD_E_INVALID_ARG : the range does not parse. */
    ShardResult (SHARD_CALL *get)(ShardPluginCtx ctx, ShardStr name, ShardStr range, ShardServiceRef* out);
    /* What the ref points to now (the caller sets out->structSize ; the host fills what fits). thread : any. SHARD_E_NOT_FOUND : its provider went away ; SHARD_E_UNSUPPORTED : the provider is not a
     * plugin of the C ABI (its object is not a ShardService). */
    ShardResult (SHARD_CALL *resolve)(ShardPluginCtx ctx, ShardServiceRef ref, ShardService* out);
    ShardResult (SHARD_CALL *release)(ShardPluginCtx ctx, ShardServiceRef ref);
} ShardServicesApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_SERVICES_H */
