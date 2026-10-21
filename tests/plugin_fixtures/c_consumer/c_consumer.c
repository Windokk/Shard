/* A second plugin in plain C : it uses the service "cplugin.math" that tests/plugin_fixtures/c_plugin provides, through the engine only.
 * It does not declare a dependency on the provider, so that disabling the provider leaves it running with an empty ref (see c_plugin_tests). */
#include <string.h>

#include <shard/shard.h>

#if defined(_WIN32)
#   define TEST_EXPORT __declspec(dllexport)
#else
#   define TEST_EXPORT __attribute__((visibility("default")))
#endif

/* The provider's table, as its header would publish it */
typedef struct MathService {
    uint32_t structSize;
    int32_t (SHARD_CALL *add)(void* self, int32_t a, int32_t b);
} MathService;

static const ShardHostApi* gHost;
static const ShardServicesApi* gServices;
static ShardPluginCtx gCtx;
static ShardServiceRef gMath;
static int gGetResult = 1;
static uint32_t gMajor, gMinor;

static ShardStr Str(const char* text)
{
    ShardStr s;
    s.data = text;
    s.size = (uint64_t)strlen(text);
    return s;
}

static ShardResult SHARD_CALL OnInitialize(void* user, ShardPluginCtx ctx)
{
    (void)user;
    gCtx = ctx;
    if (gHost->query_interface(Str(SHARD_IFACE_SERVICES), 1, 0, (const void**)&gServices) != SHARD_OK) return SHARD_E_NOT_FOUND;
    /* every plugin ran on_load : the provider's service exists now */
    gGetResult = gServices->get(ctx, Str("cplugin.math"), Str("^1.1"), &gMath);
    {
        ShardServiceRef tooNew = 0;
        if (gServices->get(ctx, Str("cplugin.math"), Str("^2"), &tooNew) != SHARD_E_NOT_FOUND) gGetResult = 1001;
        if (gServices->get(ctx, Str("cplugin.math"), Str("not a range"), &tooNew) != SHARD_E_INVALID_ARG) gGetResult = 1002;
    }
    return SHARD_OK;
}

static const ShardPluginCallbacks kCallbacks = {
    .structSize = sizeof(ShardPluginCallbacks),
    .on_initialize = &OnInitialize,
};

SHARD_PLUGIN_EXPORT ShardResult SHARD_CALL shard_plugin_entry(const ShardHostApi* host, ShardPluginInfo* out)
{
    gHost = host;
    out->structSize = sizeof(ShardPluginInfo);
    out->abiMajor = SHARD_ABI_MAJOR;
    out->abiMinor = SHARD_ABI_MINOR;
    out->id = Str("shard.test.cconsumer");
    out->callbacks = &kCallbacks;
    return SHARD_OK;
}

TEST_EXPORT int c_consumer_get_result(void) { return gGetResult; }

/* a + b through the service, or the ShardResult of resolve when it cannot */
TEST_EXPORT int c_consumer_add(int a, int b, int* out)
{
    ShardService service;
    ShardResult r;
    memset(&service, 0, sizeof(service));
    service.structSize = sizeof(service);
    r = gServices->resolve(gCtx, gMath, &service);
    if (r != SHARD_OK) return (int)r;
    gMajor = service.major;
    gMinor = service.minor;
    *out = (int)((const MathService*)service.table)->add(service.self, a, b);
    return SHARD_OK;
}

TEST_EXPORT unsigned c_consumer_version(void) { return gMajor * 100u + gMinor; }
