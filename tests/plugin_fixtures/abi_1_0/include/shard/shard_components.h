// Component types described field by field, registered as "<plugin id>.<name>".
// ECS side is plain memcpy data. The actor component mirrors the ECS values on load, edit, attach, save and copy. Entities a plugin spawns itself have no actor and are not saved.
#ifndef SHARD_COMPONENTS_H
#define SHARD_COMPONENTS_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_COMPONENTS "shard.components"

typedef uint64_t ShardComponentType;

#define SHARD_FIELD_F32     1u
#define SHARD_FIELD_F64     2u
#define SHARD_FIELD_I32     3u
#define SHARD_FIELD_U32     4u
#define SHARD_FIELD_I64     5u
#define SHARD_FIELD_U64     6u
#define SHARD_FIELD_BOOL    7u
#define SHARD_FIELD_VEC2    8u
#define SHARD_FIELD_VEC3    9u
#define SHARD_FIELD_VEC4    10u
#define SHARD_FIELD_QUAT    11u
#define SHARD_FIELD_COLOR   12u

#define SHARD_FIELD_RUNTIME     1u
#define SHARD_FIELD_PRIVATE     2u

typedef struct ShardFieldDesc {
    uint32_t structSize;
    uint32_t type;
    ShardStr name;          // letters, digits, '_'. "type", "active", "tags" reserved
    uint32_t offset;
    uint32_t flags;
    float    min, max;
    ShardStr tooltip;
} ShardFieldDesc;

typedef struct ShardComponentDesc {
    uint32_t structSize;
    uint32_t size;
    uint32_t align;
    uint32_t fieldCount;
    ShardStr name;
    const ShardFieldDesc* fields;
    const void* defaults;
} ShardComponentDesc;

#define SHARD_COMPONENT_MAX_SIZE    1024u
#define SHARD_COMPONENT_MAX_FIELDS  64u

typedef struct ShardComponentsApi {
    uint32_t structSize;

    ShardResult (SHARD_CALL *register_type)(ShardPluginCtx ctx, const ShardComponentDesc* desc, ShardComponentType* out);

    ShardResult (SHARD_CALL *find_type)(ShardPluginCtx ctx, ShardStr name, ShardComponentType* out);

    ShardResult (SHARD_CALL *type_layout)(ShardPluginCtx ctx, ShardComponentType type, uint32_t* size, uint32_t* align);

    ShardResult (SHARD_CALL *field_count)(ShardPluginCtx ctx, ShardComponentType type, uint32_t* count);
    ShardResult (SHARD_CALL *field_at)(ShardPluginCtx ctx, ShardComponentType type, uint32_t index, ShardFieldDesc* out);
    ShardResult (SHARD_CALL *find_field)(ShardPluginCtx ctx, ShardComponentType type, ShardStr name, ShardFieldDesc* out);
} ShardComponentsApi;

#ifdef __cplusplus
}
#endif

#endif
