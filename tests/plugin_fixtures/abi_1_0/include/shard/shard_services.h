// Services by name and version without including each other's code. Table starts with structSize and only grows at its end. Users hold a ShardServiceRef and resolve it before each use, so a gone provider gives SHARD_E_NOT_FOUND instead of a dangling table.
#ifndef SHARD_SERVICES_H
#define SHARD_SERVICES_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_SERVICES "shard.services"

typedef uint64_t ShardServiceRef;

typedef struct ShardService {
    uint32_t    structSize;
    uint32_t    major, minor;
    const void* table;
    void*       self;
} ShardService;

typedef struct ShardServicesApi {
    uint32_t structSize;
    ShardResult (SHARD_CALL *provide)(ShardPluginCtx ctx, ShardStr name, uint32_t major, uint32_t minor, const void* table, void* self);
    ShardResult (SHARD_CALL *get)(ShardPluginCtx ctx, ShardStr name, ShardStr range, ShardServiceRef* out);
    ShardResult (SHARD_CALL *resolve)(ShardPluginCtx ctx, ShardServiceRef ref, ShardService* out);
    ShardResult (SHARD_CALL *release)(ShardPluginCtx ctx, ShardServiceRef ref);
} ShardServicesApi;

#ifdef __cplusplus
}
#endif

#endif
