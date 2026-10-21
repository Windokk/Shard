/* A plugin written in plain C against sdk/include/shard only : no engine header, nothing of the engine linked.
 * Loaded by tests/plugins/c_plugin_tests.cpp. The exported c_plugin_* functions let the test see what the host called. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <shard/shard.h>

#if defined(_WIN32)
#   define TEST_EXPORT __declspec(dllexport)
#else
#   define TEST_EXPORT __attribute__((visibility("default")))
#endif

enum { EVENT_LOAD = 1, EVENT_INITIALIZE, EVENT_ENABLE, EVENT_DISABLE, EVENT_UNLOAD, EVENT_CAPACITY = 64 };

static const ShardHostApi* gHost;
static int gEvents[EVENT_CAPACITY];
static int gEventCount;
static void* gBlock;
static char gSeenId[128];
static char gSeenVersion[32];
static char gSeenHost[32];                  /* shard.plugin 1.1 */
static int gHasNet = -1;
static uint64_t gUser = 0x5348415244ull;    /* proves `user` comes back untouched */
static int gUserOk = 1;
static const ShardCVarsApi* gCVars;
static ShardCVar gCounter, gNote, gScale, gLocked;
static int gCVarResult;                     /* first error the cvar calls returned */
static ShardPluginCtx gCtx;
static char gEcho[128];
static int gCommandResult;

/* components, systems, ecs */
typedef struct Counter {
    float   value;
    int32_t ticks;
    float   rate;
    uint8_t enabled;
    uint8_t pad[3];
    float   axis[3];
    uint32_t scratch;           /* private : neither saved nor shown */
} Counter;

static const ShardComponentsApi* gComponents;
static const ShardEcsApi* gEcs;
static const ShardSystemsApi* gSystems;
static ShardComponentType gCounterType;
static ShardComponentType gTransformType;

/* ABI 1.2 : a component that keeps its bulk in a blob (shard.components 1.1) */
typedef struct Chunk {
    uint32_t count;
    uint32_t pad;
    ShardBlob voxels;
} Chunk;
static const ShardComponentsApi* gComponents11;
static ShardComponentType gChunkType;
static int gSetupResult;                    /* first error of the registrations in on_load */
static int gSystemRuns;
static int gUndeclaredGet;                  /* what `get` on a type the system did not declare returned */
static int gWrongThread;                    /* what a "thread : main" function called from a system returned */
static int gSpawned;
static uint64_t gChunkEntities;

/* events, hooks */
static const ShardEventsApi* gEventsApi;
static int gTicksReceived;
static int32_t gLastTicks;
static ShardSubscription gSubscription;
static int gHookCalls;

/* frames, tools, content, state */
static int gFramesBegun, gFramesEnded;
static int gContentMounted;
static char gContentText[64];
static int gContentFiles;
static int gPersist;                        /* what a reload must keep */
static int gRestored = -1;
static int gUnloadedAssets;

/* Called from every callback the host makes into the plugin : the test sees who the host says is running (crash attribution) */
static void (*gProbe)(void);
static void Probe(void)
{
    if (gProbe) gProbe();
}

static ShardStr Str(const char* text)
{
    ShardStr s;
    s.data = text;
    s.size = (uint64_t)strlen(text);
    return s;
}

static void Note(int event)
{
    if (gEventCount < EVENT_CAPACITY) gEvents[gEventCount++] = event;
}

static void CopyStr(char* to, size_t capacity, ShardStr from)
{
    size_t n = (size_t)from.size;
    if (n >= capacity) n = capacity - 1;
    if (from.data) memcpy(to, from.data, n);
    to[n] = '\0';
}

static void CheckUser(void* user)
{
    if (user != &gUser) gUserOk = 0;
}

static void Setup(ShardResult r)
{
    if (r != SHARD_OK && !gSetupResult) gSetupResult = r;
}

static const void* Query(const char* id)
{
    const void* table = NULL;
    if (gHost->query_interface(Str(id), 1, 0, &table) != SHARD_OK) return NULL;
    return table;
}

/* ---- Console ---- */

static ShardResult SHARD_CALL CmdEcho(void* user, ShardPluginCtx ctx, const ShardStr* args, uint32_t argCount, ShardStr* output)
{
    (void)ctx;
    CheckUser(user);
    Probe();
    if (argCount == 0) {
        *output = Str("usage: cplugin.echo <word>");
        return SHARD_E_INVALID_ARG;
    }
    snprintf(gEcho, sizeof(gEcho), "echo:%.*s:%u", (int)args[0].size, args[0].data, (unsigned)argCount);
    *output = Str(gEcho);
    return SHARD_OK;
}

static ShardResult SHARD_CALL CmdPersist(void* user, ShardPluginCtx ctx, const ShardStr* args, uint32_t argCount, ShardStr* output)
{
    (void)user; (void)ctx; (void)output;
    if (argCount == 1) gPersist = atoi(args[0].data);
    return SHARD_OK;
}

/* ---- The system ---- */

static ShardResult SHARD_CALL TickChunk(void* user, const ShardChunk* chunk)
{
    const float dt = *(const float*)user;
    Counter* counters = (Counter*)chunk->columns[0];
    uint32_t i;
    for (i = 0; i < chunk->count; ++i) {
        counters[i].value += counters[i].rate * dt;
        counters[i].ticks += 1;
        counters[i].scratch += 1;
    }
    gChunkEntities += chunk->count;
    return SHARD_OK;
}

static ShardResult SHARD_CALL Tick(void* user, ShardPluginCtx ctx, const ShardSystemCtx* run)
{
    ShardQuery query;
    float dt = run->deltaTime;
    (void)user;
    memset(&query, 0, sizeof(query));
    query.structSize = sizeof(query);
    query.withCount = 1;
    query.with = &gCounterType;
    gChunkEntities = 0;
    if (gEcs->each_chunk(run->ecs, &query, &TickChunk, &dt) != SHARD_OK) return SHARD_E_INTERNAL;
    ++gSystemRuns;

    {
        /* a type this system did not declare : refused */
        void* block = NULL;
        ShardEntity none = 0;
        gUndeclaredGet = gEcs->get(run->ecs, none, gTransformType, &block);
    }
    /* a main thread function from inside a system : refused */
    gWrongThread = gCVars->set_int(ctx, gCounter, 7);

    if (!gSpawned) {
        /* an entity of its own, with a Counter : created at the end of the phase */
        Counter fresh;
        ShardEntity e = 0;
        memset(&fresh, 0, sizeof(fresh));
        fresh.rate = 100.0f;
        fresh.enabled = 1;
        if (gEcs->spawn(run->ecs, &e) != SHARD_OK) return SHARD_E_INTERNAL;
        if (gEcs->add(run->ecs, e, gCounterType, &fresh, sizeof(fresh)) != SHARD_OK) return SHARD_E_INTERNAL;
        gSpawned = 1;
    }
    {
        /* queued : delivered at the end of the frame */
        int32_t ticks = gSystemRuns;
        gEventsApi->emit(ctx, Str("shard.test.cplugin.Ticked"), &ticks, sizeof(ticks));
    }
    return SHARD_OK;
}

/* ---- Events, hooks ---- */

static void SHARD_CALL OnTicked(void* user, ShardPluginCtx ctx, ShardStr name, const void* payload, uint64_t size)
{
    (void)user; (void)ctx; (void)name;
    Probe();
    ++gTicksReceived;
    if (size == sizeof(int32_t)) memcpy(&gLastTicks, payload, sizeof(int32_t));
}

static void SHARD_CALL OnPreSave(void* user, ShardPluginCtx ctx, ShardHookArgs* hook)
{
    (void)user; (void)ctx;
    Probe();
    ++gHookCalls;
    if (hook->subject.size == strlen("forbidden.world") && memcmp(hook->subject.data, "forbidden.world", hook->subject.size) == 0) {
        hook->cancelled = 1;
        hook->reason = Str("cplugin says no");
    }
}

/* ---- A service : a table other plugins call ---- */

typedef struct MathService {
    uint32_t structSize;
    int32_t (SHARD_CALL *add)(void* self, int32_t a, int32_t b);
} MathService;

static int32_t gMathBias = 1000;
static int32_t SHARD_CALL MathAdd(void* self, int32_t a, int32_t b) { return *(const int32_t*)self + a + b; }
static const MathService kMath = { sizeof(MathService), &MathAdd };

/* ---- Extension items ---- */

static ShardResult SHARD_CALL Validate(void* user, ShardPluginCtx ctx, ShardStr projectFile, const ShardReport* report)
{
    char line[256];
    (void)user; (void)ctx;
    Probe();
    snprintf(line, sizeof(line), "checked %.*s", (int)projectFile.size, projectFile.data);
    report->add(report->host, Str(line));
    return SHARD_OK;
}

static ShardResult SHARD_CALL Import(void* user, ShardPluginCtx ctx, ShardStr source, ShardStr outputDirectory, const ShardReport* report)
{
    char path[512];
    FILE* f;
    (void)user; (void)ctx;
    Probe();
    snprintf(path, sizeof(path), "%.*s/imported.txt", (int)outputDirectory.size, outputDirectory.data);
    f = fopen(path, "wb");
    if (!f) { report->message(report->host, Str("cannot write")); return SHARD_E_INTERNAL; }
    fprintf(f, "from %.*s", (int)source.size, source.data);
    fclose(f);
    report->add(report->host, Str(path));
    report->message(report->host, Str("imported"));
    return SHARD_OK;
}

static void* SHARD_CALL LoadAsset(void* user, ShardPluginCtx ctx, ShardStr path, ShardStr file)
{
    int32_t* resource;
    (void)user; (void)path; (void)file;
    Probe();
    resource = (int32_t*)gHost->alloc(ctx, sizeof(int32_t), 16);
    if (resource) *resource = 77;
    return resource;
}

static void SHARD_CALL UnloadAsset(void* user, ShardPluginCtx ctx, void* resource)
{
    (void)user;
    Probe();
    ++gUnloadedAssets;
    gHost->free(ctx, resource);
}

/* ---- The registrations ---- */

static void RegisterAll(void* user, ShardPluginCtx ctx)
{
    const ShardCommandsApi* commands = (const ShardCommandsApi*)Query(SHARD_IFACE_COMMANDS);
    const ShardHooksApi* hooks = (const ShardHooksApi*)Query(SHARD_IFACE_HOOKS);
    const ShardServicesApi* services = (const ShardServicesApi*)Query(SHARD_IFACE_SERVICES);
    const ShardExtensionsApi* extensions = (const ShardExtensionsApi*)Query(SHARD_IFACE_EXTENSIONS);

    if (commands) {
        ShardResult r = commands->add(ctx, Str("cplugin.echo"), Str("echoes its first word"), &CmdEcho, user);
        if (r != SHARD_OK) gCommandResult = r;
        if (commands->add(ctx, Str("cplugin.echo"), Str(""), &CmdEcho, user) != SHARD_E_ALREADY_EXISTS && !gCommandResult) gCommandResult = 1000;
        Setup(commands->add(ctx, Str("cplugin.persist"), Str("sets what a reload keeps"), &CmdPersist, user));
    }
    gCVars = (const ShardCVarsApi*)Query(SHARD_IFACE_CVARS);
    if (gCVars) {
        ShardResult r;
        r = gCVars->create_int(ctx, Str("counter"), 0, Str("how many times on_enable ran"), 0, 0, 100, &gCounter);
        if (r != SHARD_OK && !gCVarResult) gCVarResult = r;
        r = gCVars->create_string(ctx, Str("note"), Str("hello"), Str("a note"), 0, &gNote);
        if (r != SHARD_OK && !gCVarResult) gCVarResult = r;
        r = gCVars->create_float(ctx, Str("scale"), 1.5f, Str("a scale"), 0, 0.0f, 2.0f, &gScale);
        if (r != SHARD_OK && !gCVarResult) gCVarResult = r;
        r = gCVars->create_bool(ctx, Str("locked"), 1, Str("read only"), SHARD_CVAR_READONLY, &gLocked);
        if (r != SHARD_OK && !gCVarResult) gCVarResult = r;
        /* the same name twice must be refused */
        {
            ShardCVar again = 0;
            if (gCVars->create_int(ctx, Str("counter"), 0, Str(""), 0, 0, 1, &again) != SHARD_E_ALREADY_EXISTS && !gCVarResult) gCVarResult = 1000;
        }
    }

    /* A component : the fields the world saves, a private one it does not */
    gComponents = (const ShardComponentsApi*)Query(SHARD_IFACE_COMPONENTS);
    gEcs = (const ShardEcsApi*)Query(SHARD_IFACE_ECS);
    gSystems = (const ShardSystemsApi*)Query(SHARD_IFACE_SYSTEMS);
    if (!gComponents || !gEcs || !gSystems) { Setup(SHARD_E_NOT_FOUND); return; }
    {
        ShardFieldDesc fields[6];
        ShardComponentDesc desc;
        Counter defaults;
        memset(fields, 0, sizeof(fields));
        memset(&desc, 0, sizeof(desc));
        memset(&defaults, 0, sizeof(defaults));
        defaults.rate = 2.0f;
        defaults.enabled = 1;
        defaults.axis[1] = 1.0f;
        fields[0].structSize = sizeof(ShardFieldDesc); fields[0].type = SHARD_FIELD_F32;  fields[0].name = Str("value");   fields[0].offset = (uint32_t)offsetof(Counter, value);
        fields[1].structSize = sizeof(ShardFieldDesc); fields[1].type = SHARD_FIELD_I32;  fields[1].name = Str("ticks");   fields[1].offset = (uint32_t)offsetof(Counter, ticks);
        fields[2].structSize = sizeof(ShardFieldDesc); fields[2].type = SHARD_FIELD_F32;  fields[2].name = Str("rate");    fields[2].offset = (uint32_t)offsetof(Counter, rate);
        fields[2].min = 0.0f; fields[2].max = 10.0f; fields[2].tooltip = Str("units per second");
        fields[3].structSize = sizeof(ShardFieldDesc); fields[3].type = SHARD_FIELD_BOOL; fields[3].name = Str("enabled"); fields[3].offset = (uint32_t)offsetof(Counter, enabled);
        fields[4].structSize = sizeof(ShardFieldDesc); fields[4].type = SHARD_FIELD_VEC3; fields[4].name = Str("axis");    fields[4].offset = (uint32_t)offsetof(Counter, axis);
        fields[5].structSize = sizeof(ShardFieldDesc); fields[5].type = SHARD_FIELD_U32;  fields[5].name = Str("scratch"); fields[5].offset = (uint32_t)offsetof(Counter, scratch);
        fields[5].flags = SHARD_FIELD_PRIVATE;
        desc.structSize = sizeof(desc);
        desc.name = Str("Counter");
        desc.size = sizeof(Counter);
        desc.align = 4;
        desc.fieldCount = 6;
        desc.fields = fields;
        desc.defaults = &defaults;
        Setup(gComponents->register_type(ctx, &desc, &gCounterType));
        {
            /* the same name again, and a field that goes past the block : refused */
            ShardComponentType again = 0;
            if (gComponents->register_type(ctx, &desc, &again) != SHARD_E_ALREADY_EXISTS) Setup(1001);
            desc.name = Str("Broken");
            fields[0].offset = sizeof(Counter);
            if (gComponents->register_type(ctx, &desc, &again) != SHARD_E_INVALID_ARG) Setup(1002);
        }
        Setup(gComponents->find_type(ctx, Str("shard.LocalTransform"), &gTransformType));
    }
    if (gHost->query_interface(Str(SHARD_IFACE_COMPONENTS), 1, 1, (const void**)&gComponents11) == SHARD_OK && gComponents11) {
        ShardFieldDesc fields[2];
        ShardComponentDesc desc;
        memset(fields, 0, sizeof(fields));
        memset(&desc, 0, sizeof(desc));
        fields[0].structSize = sizeof(ShardFieldDesc); fields[0].type = SHARD_FIELD_U32;  fields[0].name = Str("count");  fields[0].offset = (uint32_t)offsetof(Chunk, count);
        fields[1].structSize = sizeof(ShardFieldDesc); fields[1].type = SHARD_FIELD_BLOB; fields[1].name = Str("voxels"); fields[1].offset = (uint32_t)offsetof(Chunk, voxels);
        desc.structSize = sizeof(desc);
        desc.name = Str("Chunk");
        desc.size = sizeof(Chunk);
        desc.align = 8;
        desc.fieldCount = 2;
        desc.fields = fields;
        Setup(gComponents11->register_type(ctx, &desc, &gChunkType));
    } else {
        Setup(SHARD_E_NOT_FOUND);
    }
    {
        ShardSystemDesc system;
        memset(&system, 0, sizeof(system));
        system.structSize = sizeof(system);
        system.name = Str("tick");
        system.phase = SHARD_PHASE_UPDATE;
        system.writeCount = 1;
        system.writes = &gCounterType;
        system.run = &Tick;
        system.user = user;
        Setup(gSystems->add(ctx, &system));
        if (gSystems->add(ctx, &system) != SHARD_E_ALREADY_EXISTS) Setup(1003);
    }

    gEventsApi = (const ShardEventsApi*)Query(SHARD_IFACE_EVENTS);
    if (gEventsApi) Setup(gEventsApi->subscribe(ctx, Str("shard.test.cplugin.Ticked"), &OnTicked, user, &gSubscription));
    {
        /* shard.events 1.1 : what Ticked carries, for the tools */
        const ShardEventsApi* events11 = NULL;
        if (gHost->query_interface(Str(SHARD_IFACE_EVENTS), 1, 1, (const void**)&events11) == SHARD_OK && events11) {
            ShardFieldDesc ticks;
            memset(&ticks, 0, sizeof(ticks));
            ticks.structSize = sizeof(ticks);
            ticks.type = SHARD_FIELD_I32;
            ticks.name = Str("ticks");
            Setup(events11->describe(ctx, Str("shard.test.cplugin.Ticked"), sizeof(int32_t), &ticks, 1));
            ticks.offset = 2;   /* past the end of the payload : refused */
            if (events11->describe(ctx, Str("shard.test.cplugin.Broken"), sizeof(int32_t), &ticks, 1) != SHARD_E_INVALID_ARG) Setup(1005);
        } else {
            Setup(SHARD_E_NOT_FOUND);
        }
    }
    if (hooks) {
        ShardHookToken token = 0;
        Setup(hooks->on(ctx, Str("PreSave"), SHARD_PRIORITY_NORMAL, &OnPreSave, user, &token));
        if (hooks->on(ctx, Str("NoSuchHook"), 0, &OnPreSave, user, &token) != SHARD_E_NOT_FOUND) Setup(1004);
    }
    if (services) Setup(services->provide(ctx, Str("cplugin.math"), 1, 2, &kMath, &gMathBias));
    if (extensions) {
        ShardValidatorItem validator;
        ShardImporterItem importer;
        ShardAssetTypeItem assetType;
        ShardStr importExtensions[1];
        ShardStr assetExtensions[1];
        memset(&validator, 0, sizeof(validator));
        validator.structSize = sizeof(validator);
        validator.name = Str("cplugin.check");
        validator.validate = &Validate;
        Setup(extensions->add_validator(ctx, &validator));

        importExtensions[0] = Str(".cpin");
        memset(&importer, 0, sizeof(importer));
        importer.structSize = sizeof(importer);
        importer.name = Str("cplugin.importer");
        importer.extensions = importExtensions;
        importer.extensionCount = 1;
        importer.import = &Import;
        Setup(extensions->add_importer(ctx, &importer));

        assetExtensions[0] = Str(".cpasset");
        memset(&assetType, 0, sizeof(assetType));
        assetType.structSize = sizeof(assetType);
        assetType.name = Str("cplugin.asset");
        assetType.extensions = assetExtensions;
        assetType.extensionCount = 1;
        assetType.load = &LoadAsset;
        assetType.unload = &UnloadAsset;
        Setup(extensions->add_asset_type(ctx, &assetType));
    }
}

/* ---- Callbacks ---- */

static ShardResult SHARD_CALL OnLoad(void* user, ShardPluginCtx ctx)
{
    const ShardPluginApi* plugin = NULL;
    CheckUser(user);
    /* 1.1 of the interface : host and has_permission */
    gHost->query_interface(Str(SHARD_IFACE_PLUGIN), 1, 1, (const void**)&plugin);
    Note(EVENT_LOAD);
    gCtx = ctx;
    gSpawned = 0;
    if (plugin) {
        CopyStr(gSeenId, sizeof(gSeenId), plugin->id(ctx));
        CopyStr(gSeenVersion, sizeof(gSeenVersion), plugin->version(ctx));
        CopyStr(gSeenHost, sizeof(gSeenHost), plugin->host(ctx));
        gHasNet = plugin->has_permission(ctx, Str("net"));
    }
    gHost->log(ctx, SHARD_LOG_INFO, Str("[CPLUGIN] loaded"));
    RegisterAll(user, ctx);
    return SHARD_OK;
}

static ShardResult SHARD_CALL OnInitialize(void* user, ShardPluginCtx ctx)
{
    (void)ctx;
    CheckUser(user);
    Note(EVENT_INITIALIZE);
    return SHARD_OK;
}

static ShardResult SHARD_CALL OnEnable(void* user, ShardPluginCtx ctx)
{
    CheckUser(user);
    Note(EVENT_ENABLE);
    /* Memory goes through the host so it is counted against the budget */
    if (!gBlock) gBlock = gHost->alloc(ctx, 1024, 16);
    if (gCVars && gCounter) {
        int32_t counter = 0;
        if (gCVars->get_int(ctx, gCounter, &counter) == SHARD_OK) gCVars->set_int(ctx, gCounter, counter + 1);
        gCVars->set_string(ctx, gNote, Str("enabled"));
        gCVars->set_float(ctx, gScale, 9.0f);       /* above the maximum : clamped by the host */
        gCVars->set_bool(ctx, gLocked, 0);          /* the owner may change its own read-only variable */
    }
    return gBlock ? SHARD_OK : SHARD_E_OUT_OF_MEMORY;
}

static ShardResult SHARD_CALL OnDisable(void* user, ShardPluginCtx ctx)
{
    CheckUser(user);
    Note(EVENT_DISABLE);
    if (gBlock) { gHost->free(ctx, gBlock); gBlock = NULL; }
    return SHARD_OK;
}

static ShardResult SHARD_CALL OnUnload(void* user, ShardPluginCtx ctx)
{
    CheckUser(user);
    Note(EVENT_UNLOAD);
    if (gBlock) { gHost->free(ctx, gBlock); gBlock = NULL; }
    return SHARD_OK;
}

static ShardResult SHARD_CALL OnPrepareReload(void* user, ShardPluginCtx ctx, ShardState state)
{
    const ShardStateApi* api = (const ShardStateApi*)Query(SHARD_IFACE_STATE);
    char json[64];
    (void)user; (void)ctx;
    if (!api) return SHARD_E_UNSUPPORTED;
    snprintf(json, sizeof(json), "{\"persist\": %d}", gPersist);
    if (api->set(state, Str("saved"), Str(json)) != SHARD_OK) return SHARD_E_INTERNAL;
    if (api->set(state, Str("broken"), Str("{not json")) != SHARD_E_INVALID_ARG) return SHARD_E_INTERNAL;
    return SHARD_OK;
}

static ShardResult SHARD_CALL OnRestore(void* user, ShardPluginCtx ctx, ShardState state)
{
    const ShardStateApi* api = (const ShardStateApi*)Query(SHARD_IFACE_STATE);
    char json[64];
    uint64_t size = 0;
    const char* at;
    (void)user; (void)ctx;
    if (!api || !api->has(state, Str("saved"))) return SHARD_E_NOT_FOUND;
    if (api->get(state, Str("saved"), json, sizeof(json) - 1, &size) != SHARD_OK) return SHARD_E_INTERNAL;
    json[size < sizeof(json) - 1 ? size : sizeof(json) - 1] = '\0';
    at = strchr(json, ':');
    gRestored = at ? atoi(at + 1) : -2;
    gPersist = gRestored;
    return SHARD_OK;
}

static ShardResult SHARD_CALL OnFrameBegin(void* user, ShardPluginCtx ctx, uint64_t frame)
{
    (void)user; (void)ctx; (void)frame;
    ++gFramesBegun;
    return SHARD_OK;
}

static ShardResult SHARD_CALL OnFrameEnd(void* user, ShardPluginCtx ctx, uint64_t frame)
{
    (void)user; (void)ctx; (void)frame;
    ++gFramesEnded;
    return SHARD_OK;
}

static ShardResult SHARD_CALL CountFile(void* user, ShardStr path)
{
    (void)user; (void)path;
    ++gContentFiles;
    return SHARD_OK;
}

static ShardResult SHARD_CALL OnContentMounted(void* user, ShardPluginCtx ctx)
{
    const ShardContentApi* content = (const ShardContentApi*)Query(SHARD_IFACE_CONTENT);
    char uri[256];
    uint64_t size = 0;
    void* data = NULL;
    (void)user;
    ++gContentMounted;
    if (!content) return SHARD_E_NOT_FOUND;
    if (content->uri_of(ctx, Str("hello.txt"), uri, sizeof(uri) - 1, &size) != SHARD_OK) return SHARD_E_INTERNAL;
    uri[size] = '\0';
    if (content->read(ctx, Str(uri), &data, &size) != SHARD_OK) return SHARD_E_INTERNAL;
    CopyStr(gContentText, sizeof(gContentText), (ShardStr){ (const char*)data, size });
    gHost->free(ctx, data);
    gContentFiles = 0;
    return content->list(ctx, Str(""), Str(""), &CountFile, NULL);
}

static ShardResult SHARD_CALL OnToolTask(void* user, ShardPluginCtx ctx, ShardStr task, ShardStr project, ShardStr* message)
{
    (void)user; (void)ctx; (void)project;
    if (task.size == 4 && memcmp(task.data, "cook", 4) == 0) {
        *message = Str("cplugin refuses cooking");
        return SHARD_E_UNSUPPORTED;
    }
    return SHARD_OK;
}

static const ShardPluginCallbacks kCallbacks = {
    .structSize = sizeof(ShardPluginCallbacks),
    .on_load = &OnLoad,
    .on_initialize = &OnInitialize,
    .on_enable = &OnEnable,
    .on_disable = &OnDisable,
    .on_unload = &OnUnload,
    .on_prepare_reload = &OnPrepareReload,
    .on_restore = &OnRestore,
    .on_frame_begin = &OnFrameBegin,
    .on_frame_end = &OnFrameEnd,
    .on_content_mounted = &OnContentMounted,
    .on_tool_task = &OnToolTask,
};

static void Fill(ShardPluginInfo* out)
{
    out->structSize = sizeof(ShardPluginInfo);
    out->abiMajor = SHARD_ABI_MAJOR;
    out->abiMinor = SHARD_ABI_MINOR;
    out->id = Str("shard.test.cplugin");
    out->user = &gUser;
    out->callbacks = &kCallbacks;
}

SHARD_PLUGIN_EXPORT ShardResult SHARD_CALL shard_plugin_entry(const ShardHostApi* host, ShardPluginInfo* out)
{
    if (!host || !out) return SHARD_E_INVALID_ARG;
    gHost = host;
    gEventCount = 0;
    Fill(out);
    return SHARD_OK;
}

/* ---- Entry points that must be refused : a manifest picks one with "entry" ---- */

SHARD_PLUGIN_EXPORT ShardResult SHARD_CALL shard_plugin_entry_future(const ShardHostApi* host, ShardPluginInfo* out)
{
    ShardResult result = shard_plugin_entry(host, out);
    out->abiMinor = SHARD_ABI_MINOR + 1;
    return result;
}

SHARD_PLUGIN_EXPORT ShardResult SHARD_CALL shard_plugin_entry_other_major(const ShardHostApi* host, ShardPluginInfo* out)
{
    ShardResult result = shard_plugin_entry(host, out);
    out->abiMajor = SHARD_ABI_MAJOR + 1;
    return result;
}

SHARD_PLUGIN_EXPORT ShardResult SHARD_CALL shard_plugin_entry_small(const ShardHostApi* host, ShardPluginInfo* out)
{
    ShardResult result = shard_plugin_entry(host, out);
    out->structSize = 8;
    return result;
}

SHARD_PLUGIN_EXPORT ShardResult SHARD_CALL shard_plugin_entry_no_callbacks(const ShardHostApi* host, ShardPluginInfo* out)
{
    ShardResult result = shard_plugin_entry(host, out);
    out->callbacks = NULL;
    return result;
}

SHARD_PLUGIN_EXPORT ShardResult SHARD_CALL shard_plugin_entry_refuse(const ShardHostApi* host, ShardPluginInfo* out)
{
    (void)host;
    (void)out;
    return SHARD_E_UNSUPPORTED;
}

/* ---- What the test reads ---- */

TEST_EXPORT void c_plugin_set_probe(void (*probe)(void)) { gProbe = probe; }
TEST_EXPORT int c_plugin_event_count(void) { return gEventCount; }
TEST_EXPORT int c_plugin_event(int index) { return index >= 0 && index < gEventCount ? gEvents[index] : 0; }
TEST_EXPORT const char* c_plugin_seen_id(void) { return gSeenId; }
TEST_EXPORT const char* c_plugin_seen_version(void) { return gSeenVersion; }
TEST_EXPORT const char* c_plugin_seen_host(void) { return gSeenHost; }
TEST_EXPORT int c_plugin_has_net(void) { return gHasNet; }
TEST_EXPORT int c_plugin_user_ok(void) { return gUserOk; }
TEST_EXPORT int c_plugin_holds_block(void) { return gBlock != NULL; }
TEST_EXPORT int c_plugin_cvar_error(void) { return gCVarResult; }
/* what a read of a variable of the previous enable returns once the host destroyed it (the handle is stale) */
TEST_EXPORT int c_plugin_read_old_counter(void)
{
    int32_t value = 0;
    if (!gCVars) return 1;
    return (int)gCVars->get_int(gCtx, gCounter, &value);
}
TEST_EXPORT int c_plugin_cvar_counter(void)
{
    int32_t value = -1;
    if (!gCVars || gCVars->get_int(gCtx, gCounter, &value) != SHARD_OK) return -1;
    return (int)value;
}
TEST_EXPORT int c_plugin_command_error(void) { return gCommandResult; }
TEST_EXPORT int c_plugin_setup_error(void) { return gSetupResult; }
TEST_EXPORT uint64_t c_plugin_counter_type(void) { return gCounterType; }
TEST_EXPORT int c_plugin_system_runs(void) { return gSystemRuns; }
TEST_EXPORT uint64_t c_plugin_chunk_entities(void) { return gChunkEntities; }
TEST_EXPORT int c_plugin_undeclared_get(void) { return gUndeclaredGet; }
TEST_EXPORT int c_plugin_wrong_thread(void) { return gWrongThread; }
TEST_EXPORT int c_plugin_ticks_received(void) { return gTicksReceived; }
TEST_EXPORT int c_plugin_last_ticks(void) { return gLastTicks; }
TEST_EXPORT int c_plugin_hook_calls(void) { return gHookCalls; }
TEST_EXPORT int c_plugin_frames_begun(void) { return gFramesBegun; }
TEST_EXPORT int c_plugin_frames_ended(void) { return gFramesEnded; }
TEST_EXPORT int c_plugin_content_mounted(void) { return gContentMounted; }
TEST_EXPORT const char* c_plugin_content_text(void) { return gContentText; }
TEST_EXPORT int c_plugin_content_files(void) { return gContentFiles; }
TEST_EXPORT int c_plugin_restored(void) { return gRestored; }
TEST_EXPORT int c_plugin_unloaded_assets(void) { return gUnloadedAssets; }
/* the subscription stops : no more Ticked */
TEST_EXPORT int c_plugin_unsubscribe(void)
{
    return gEventsApi ? (int)gEventsApi->unsubscribe(gCtx, gSubscription) : 1;
}
/* how another plugin's type looks from here */
TEST_EXPORT uint64_t c_plugin_chunk_type(void) { return gChunkType; }

/* A blob of n bytes holding i % 251 at byte i (0 on failure) */
TEST_EXPORT uint64_t c_plugin_make_blob(uint32_t n)
{
    ShardBlob blob = 0;
    void* data = NULL;
    uint64_t size = 0;
    uint32_t i;
    if (gComponents11->blob_create(gCtx, n, &blob) != SHARD_OK) return 0;
    if (gComponents11->blob_data(gCtx, blob, &data, &size) != SHARD_OK || size != n) return 0;
    for (i = 0; i < n; ++i) ((unsigned char*)data)[i] = (unsigned char)(i % 251);
    return blob;
}

/* SHARD_OK if the blob holds what c_plugin_make_blob(n) wrote ; its error otherwise, 1 for other bytes */
TEST_EXPORT int c_plugin_check_blob(uint64_t blob, uint32_t n)
{
    void* data = NULL;
    uint64_t size = 0;
    uint32_t i;
    ShardResult r = gComponents11->blob_data(gCtx, blob, &data, &size);
    if (r != SHARD_OK) return (int)r;
    if (size != n) return 1;
    for (i = 0; i < n; ++i) if (((const unsigned char*)data)[i] != (unsigned char)(i % 251)) return 1;
    return SHARD_OK;
}

/* Resizes, then frees : the results of both (0 when both worked) */
TEST_EXPORT int c_plugin_resize_and_free(uint64_t blob, uint32_t n)
{
    ShardResult r = gComponents11->blob_resize(gCtx, blob, n);
    if (r != SHARD_OK) return (int)r;
    return (int)gComponents11->blob_free(gCtx, blob);
}

/* shard.experimental.world_ecs : counts the entities that have a Counter from the main thread, then spawns one more (applied at close) */
TEST_EXPORT int c_plugin_world_count(uint64_t* count)
{
    const ShardWorldEcsApi* world = NULL;
    ShardEcs ecs = 0;
    ShardQuery query;
    ShardEntity e = 0;
    Counter fresh;
    ShardResult r;
    if (gHost->query_interface(Str(SHARD_IFACE_EXPERIMENTAL_WORLD_ECS), 0, 1, (const void**)&world) != SHARD_OK) return SHARD_E_NOT_FOUND;
    r = world->open(gCtx, &ecs);
    if (r != SHARD_OK) return (int)r;
    memset(&query, 0, sizeof(query));
    query.structSize = sizeof(query);
    query.withCount = 1;
    query.with = &gCounterType;
    r = gEcs->count(ecs, &query, count);
    /* a main thread function keeps working while the registry is open */
    if (r == SHARD_OK && gCVars->set_int(gCtx, gCounter, 3) != SHARD_OK) r = 1000;
    memset(&fresh, 0, sizeof(fresh));
    if (r == SHARD_OK) r = gEcs->spawn(ecs, &e);
    if (r == SHARD_OK) r = gEcs->add(ecs, e, gCounterType, &fresh, sizeof(fresh));
    if (world->close(gCtx, ecs) != SHARD_OK && r == SHARD_OK) r = 1001;
    if (gEcs->count(ecs, &query, count) != SHARD_E_STALE_HANDLE && r == SHARD_OK) r = 1002;     /* closed : stale */
    return (int)r;
}

TEST_EXPORT int c_plugin_describe_field(const char* name, uint32_t* offset, uint32_t* type)
{
    ShardFieldDesc field;
    ShardResult r;
    memset(&field, 0, sizeof(field));
    field.structSize = sizeof(field);
    r = gComponents->find_field(gCtx, gCounterType, Str(name), &field);
    *offset = field.offset;
    *type = field.type;
    return (int)r;
}
