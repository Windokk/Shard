/* A plugin written in plain C against sdk/include/shard only : no engine header, nothing of the engine linked.
 * Loaded by tests/plugins/c_plugin_tests.cpp. The exported c_plugin_* functions let the test see what the host called. */
#include <stdio.h>
#include <string.h>

#include <shard/shard_commands.h>
#include <shard/shard_cvars.h>
#include <shard/shard_host.h>
#include <shard/shard_plugin.h>

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
static uint64_t gUser = 0x5348415244ull;    /* proves `user` comes back untouched */
static int gUserOk = 1;
static const ShardCVarsApi* gCVars;
static ShardCVar gCounter, gNote, gScale, gLocked;
static int gCVarResult;                     /* first error the cvar calls returned */
static ShardPluginCtx gCtx;
static char gEcho[128];
static int gCommandResult;

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

static ShardResult SHARD_CALL CmdEcho(void* user, ShardPluginCtx ctx, const ShardStr* args, uint32_t argCount, ShardStr* output)
{
    (void)ctx;
    CheckUser(user);
    if (argCount == 0) {
        *output = Str("usage: cplugin.echo <word>");
        return SHARD_E_INVALID_ARG;
    }
    snprintf(gEcho, sizeof(gEcho), "echo:%.*s:%u", (int)args[0].size, args[0].data, (unsigned)argCount);
    *output = Str(gEcho);
    return SHARD_OK;
}

static ShardResult SHARD_CALL OnLoad(void* user, ShardPluginCtx ctx)
{
    const ShardPluginApi* plugin = NULL;
    CheckUser(user);
    Note(EVENT_LOAD);
    gCtx = ctx;
    if (gHost->query_interface(Str(SHARD_IFACE_PLUGIN), 1, 0, (const void**)&plugin) == SHARD_OK && plugin) {
        CopyStr(gSeenId, sizeof(gSeenId), plugin->id(ctx));
        CopyStr(gSeenVersion, sizeof(gSeenVersion), plugin->version(ctx));
    }
    gHost->log(ctx, SHARD_LOG_INFO, Str("[CPLUGIN] loaded"));

    {
        const ShardCommandsApi* commands = NULL;
        if (gHost->query_interface(Str(SHARD_IFACE_COMMANDS), 1, 0, (const void**)&commands) == SHARD_OK && commands) {
            ShardResult r = commands->add(ctx, Str("cplugin.echo"), Str("echoes its first word"), &CmdEcho, user);
            if (r != SHARD_OK) gCommandResult = r;
            if (commands->add(ctx, Str("cplugin.echo"), Str(""), &CmdEcho, user) != SHARD_E_ALREADY_EXISTS && !gCommandResult) gCommandResult = 1000;
        }
    }
    if (gHost->query_interface(Str(SHARD_IFACE_CVARS), 1, 0, (const void**)&gCVars) == SHARD_OK && gCVars) {
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

static const ShardPluginCallbacks kCallbacks = {
    sizeof(ShardPluginCallbacks), &OnLoad, &OnInitialize, &OnEnable, &OnDisable, &OnUnload
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

TEST_EXPORT int c_plugin_event_count(void) { return gEventCount; }
TEST_EXPORT int c_plugin_event(int index) { return index >= 0 && index < gEventCount ? gEvents[index] : 0; }
TEST_EXPORT const char* c_plugin_seen_id(void) { return gSeenId; }
TEST_EXPORT const char* c_plugin_seen_version(void) { return gSeenVersion; }
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
