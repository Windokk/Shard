/* A plugin built against the FROZEN headers of the C ABI 1.0 (tests/plugin_fixtures/abi_1_0/include) : it has to keep loading and working in
 * every engine of ABI 1.x. Loaded by tests/plugins/abi_compat_tests.cpp. */
#include <stddef.h>
#include <string.h>

#include <shard/shard.h>

#if defined(_WIN32)
#   define TEST_EXPORT __declspec(dllexport)
#else
#   define TEST_EXPORT __attribute__((visibility("default")))
#endif

typedef struct Old {
    float value;
    int32_t runs;
} Old;

static const ShardHostApi* gHost;
static const ShardEcsApi* gEcs;
static ShardComponentType gOld;
static int gSetup = 1;
static int gRuns;
static uint32_t gPluginApiSize;     /* the structSize of the shard.plugin table the host gave */
static char gId[64];

static ShardStr Str(const char* text)
{
    ShardStr s;
    s.data = text;
    s.size = (uint64_t)strlen(text);
    return s;
}

static ShardResult SHARD_CALL Bump(void* user, const ShardChunk* chunk)
{
    Old* blocks = (Old*)chunk->columns[0];
    uint32_t i;
    (void)user;
    for (i = 0; i < chunk->count; ++i) { blocks[i].value += 1.0f; blocks[i].runs += 1; }
    return SHARD_OK;
}

static ShardResult SHARD_CALL Run(void* user, ShardPluginCtx ctx, const ShardSystemCtx* run)
{
    ShardQuery query;
    (void)user; (void)ctx;
    memset(&query, 0, sizeof(query));
    query.structSize = sizeof(query);
    query.withCount = 1;
    query.with = &gOld;
    ++gRuns;
    return gEcs->each_chunk(run->ecs, &query, &Bump, NULL);
}

static ShardResult SHARD_CALL OnLoad(void* user, ShardPluginCtx ctx)
{
    const ShardPluginApi* plugin = NULL;
    const ShardComponentsApi* components = NULL;
    const ShardSystemsApi* systems = NULL;
    ShardFieldDesc fields[2];
    ShardComponentDesc desc;
    ShardSystemDesc system;
    (void)user;

    if (gHost->query_interface(Str(SHARD_IFACE_PLUGIN), 1, 0, (const void**)&plugin) != SHARD_OK) return SHARD_E_NOT_FOUND;
    gPluginApiSize = plugin->structSize;
    {
        ShardStr id = plugin->id(ctx);
        size_t n = id.size < sizeof(gId) - 1 ? (size_t)id.size : sizeof(gId) - 1;
        memcpy(gId, id.data, n);
        gId[n] = '\0';
    }
    if (gHost->query_interface(Str(SHARD_IFACE_COMPONENTS), 1, 0, (const void**)&components) != SHARD_OK) return SHARD_E_NOT_FOUND;
    if (gHost->query_interface(Str(SHARD_IFACE_SYSTEMS), 1, 0, (const void**)&systems) != SHARD_OK) return SHARD_E_NOT_FOUND;
    if (gHost->query_interface(Str(SHARD_IFACE_ECS), 1, 0, (const void**)&gEcs) != SHARD_OK) return SHARD_E_NOT_FOUND;

    memset(fields, 0, sizeof(fields));
    fields[0].structSize = sizeof(ShardFieldDesc); fields[0].type = SHARD_FIELD_F32; fields[0].name = Str("value"); fields[0].offset = (uint32_t)offsetof(Old, value);
    fields[1].structSize = sizeof(ShardFieldDesc); fields[1].type = SHARD_FIELD_I32; fields[1].name = Str("runs");  fields[1].offset = (uint32_t)offsetof(Old, runs);
    memset(&desc, 0, sizeof(desc));
    desc.structSize = sizeof(desc);
    desc.name = Str("Old");
    desc.size = sizeof(Old);
    desc.align = 4;
    desc.fieldCount = 2;
    desc.fields = fields;
    if (components->register_type(ctx, &desc, &gOld) != SHARD_OK) return SHARD_E_INTERNAL;

    memset(&system, 0, sizeof(system));
    system.structSize = sizeof(system);
    system.name = Str("bump");
    system.phase = SHARD_PHASE_UPDATE;
    system.writeCount = 1;
    system.writes = &gOld;
    system.run = &Run;
    if (systems->add(ctx, &system) != SHARD_OK) return SHARD_E_INTERNAL;
    gSetup = 0;
    return SHARD_OK;
}

static const ShardPluginCallbacks kCallbacks = { .structSize = sizeof(ShardPluginCallbacks), .on_load = &OnLoad };

SHARD_PLUGIN_EXPORT ShardResult SHARD_CALL shard_plugin_entry(const ShardHostApi* host, ShardPluginInfo* out)
{
    gHost = host;
    out->structSize = sizeof(ShardPluginInfo);
    out->abiMajor = SHARD_ABI_MAJOR;
    out->abiMinor = SHARD_ABI_MINOR;
    out->id = Str("shard.test.abi10");
    out->callbacks = &kCallbacks;
    return SHARD_OK;
}

TEST_EXPORT int abi10_setup(void) { return gSetup; }
TEST_EXPORT int abi10_runs(void) { return gRuns; }
TEST_EXPORT uint64_t abi10_type(void) { return gOld; }
TEST_EXPORT const char* abi10_id(void) { return gId; }
TEST_EXPORT unsigned abi10_built_minor(void) { return SHARD_ABI_MINOR; }
TEST_EXPORT unsigned abi10_own_plugin_api_size(void) { return (unsigned)sizeof(ShardPluginApi); }
TEST_EXPORT unsigned abi10_host_plugin_api_size(void) { return gPluginApiSize; }
TEST_EXPORT unsigned abi10_own_callbacks_size(void) { return (unsigned)sizeof(ShardPluginCallbacks); }
