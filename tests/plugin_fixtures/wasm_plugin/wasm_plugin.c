/* A sandboxed plugin in C (tier 2, abi wasm1) : a plugin written for the C ABI like any other, compiled by Clang for wasm32 with no libc and
 * linked by wasm-ld into test_plugins/shard.test.wasm (tests/CMakeLists.txt). tests/plugins/wasm_sdk_plugin_tests.cpp loads it with the real
 * PluginManager and calls the exported wasm_* functions to see what it did. It uses most of the interfaces the sandbox offers : the host's
 * answers are checked from inside (CHECK counts the failures), the effects on the world from the outside.
 *
 * SHARD_WASM_CHECK_LAYOUT compiles the assertions of shard_wasm_imports.h : the wasm32 layouts the host reads are the ones Clang gives. */
#define SHARD_WASM_CHECK_LAYOUT
#include <shard/shard.h>

#include <stddef.h>

#include "wasm_stats.h"

#define EXPORT(name) __attribute__((export_name(name)))

/* ---- no libc : what Clang may call for a struct copy, and the few string helpers ---- */
void* memcpy(void* to, const void* from, size_t n) { char* d = (char*)to; const char* s = (const char*)from; while (n--) *d++ = *s++; return to; }
void* memset(void* to, int value, size_t n) { char* d = (char*)to; while (n--) *d++ = (char)value; return to; }
int memcmp(const void* a, const void* b, size_t n)
{
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    for (; n; --n, ++x, ++y) if (*x != *y) return *x < *y ? -1 : 1;
    return 0;
}
static size_t length(const char* s) { size_t n = 0; while (s[n]) ++n; return n; }
static ShardStr S(const char* text) { ShardStr s; s.data = text; s.size = length(text); return s; }
static int equal(ShardStr a, const char* text) { return a.size == length(text) && memcmp(a.data, text, (size_t)a.size) == 0; }

/* ---- what the plugin counts, read by the test through wasm_stat ---- */
static int g_stats[S_STAT_COUNT];
EXPORT("wasm_stat") int wasm_stat(int which) { return which >= 0 && which < S_STAT_COUNT ? g_stats[which] : -1; }

static const ShardHostApi* g_host;
static ShardPluginCtx g_ctx;

static void say(uint32_t level, const char* text) { g_host->log(g_ctx, level, S(text)); }
static void fail(const char* what, int line)
{
    char text[160] = "check failed : ";
    size_t n = length(text), i = 0;
    while (what[i] && n < 130) text[n++] = what[i++];
    text[n++] = ' ';
    text[n++] = '(';
    char digits[12];
    int d = 0;
    for (int v = line; v > 0; v /= 10) digits[d++] = (char)('0' + v % 10);
    while (d > 0) text[n++] = digits[--d];
    text[n++] = ')';
    text[n] = 0;
    say(SHARD_LOG_ERROR, text);
}
#define CHECK(condition) do { ++g_stats[S_CHECKS]; if (!(condition)) { ++g_stats[S_FAILURES]; fail(#condition, __LINE__); } } while (0)

static const void* iface(const char* id, uint32_t minor)
{
    const void* table = 0;
    if (g_host->query_interface(S(id), 1, minor, &table) != SHARD_OK) return 0;
    return table;
}

static const ShardPluginApi* g_plugin;
static const ShardCVarsApi* g_cvars;
static const ShardCommandsApi* g_commands;
static const ShardComponentsApi* g_components;
static const ShardEcsApi* g_ecs;
static const ShardSystemsApi* g_systems;
static const ShardEventsApi* g_events;
static const ShardContentApi* g_content;
static const ShardStateApi* g_state;
static const ShardActorsApi* g_actors;
static const ShardScriptsApi* g_scripts;
static const ShardInputApi* g_input;

/* ---- components : a block with a number, a speed, a text and a blob ---- */
typedef struct Spin {
    float angle;
    float speed;
    ShardBlob note;
    ShardBlob bulk;
} Spin;
static ShardComponentType g_spinType;
static float g_delta;

static ShardEcs g_runEcs;
static ShardResult spin_chunk(void* user, const ShardChunk* chunk)
{
    Spin* spins = (Spin*)chunk->columns[0];
    uint32_t i;
    (void)user;
    for (i = 0; i < chunk->count; ++i) spins[i].angle += spins[i].speed * g_delta;
    g_stats[S_SPIN_ENTITIES] += (int)chunk->count;
    if (chunk->count) {
        /* the same block reached another way is the same block */
        void* block = 0;
        if (g_ecs->get(g_runEcs, chunk->entities[0], g_spinType, &block) == SHARD_OK && block == (void*)&spins[0]) ++g_stats[S_SPIN_SAME_BLOCK];
    }
    return SHARD_OK;
}

static ShardResult spin_run(void* user, ShardPluginCtx ctx, const ShardSystemCtx* run)
{
    ShardQuery query;
    ShardComponentType with[1];
    (void)user; (void)ctx;
    with[0] = g_spinType;
    query.structSize = sizeof(query);
    query.withCount = 1;
    query.withoutCount = 0;
    query.with = with;
    query.without = 0;
    g_delta = run->deltaTime;
    g_runEcs = run->ecs;
    ++g_stats[S_SPIN_RUNS];
    return g_ecs->each_chunk(run->ecs, &query, spin_chunk, 0);
}

/* ---- events : a handler that is called while the module is inside emit ---- */
static int g_in_emit;
static ShardSubscription g_ping_subscription;
static void on_ping(void* user, ShardPluginCtx ctx, ShardStr name, const void* payload, uint64_t size)
{
    (void)user; (void)ctx;
    if (equal(name, "sandbox.ping") && size == 4) {
        int32_t value;
        memcpy(&value, payload, 4);
        g_stats[S_PING_VALUE] = value;
        ++g_stats[S_PING];
        if (g_in_emit) ++g_stats[S_REENTRANT];
    }
}

/* ---- scripts ---- */
typedef struct Mover {
    float speed;                /* a field */
    ShardBlob label;            /* a text field : a blob the host owns */
    int32_t icon;               /* an asset field */
    int32_t ticks;              /* a runtime field */
    ShardActor owner;           /* the module's own state : no field, the host never sees it */
    int32_t secret;
    int32_t pad;
} Mover;

typedef struct Counter {
    int32_t count;
} Counter;

static int g_moverConstructed;

static ShardResult mover_construct(void* user, void* storage)
{
    Mover* m = (Mover*)storage;
    (void)user;
    m->speed = 2.0f;
    m->label = 0;
    m->icon = 0;
    m->ticks = 0;
    m->owner = 0;
    m->secret = 100;
    ++g_stats[S_CONSTRUCTED];
    ++g_moverConstructed;
    return SHARD_OK;
}
static void mover_destruct(void* user, void* storage)
{
    (void)user; (void)storage;
    ++g_stats[S_DESTRUCTED];
}
static ShardResult mover_attach(void* user, void* object, ShardActor owner)
{
    (void)user;
    ((Mover*)object)->owner = owner;
    return SHARD_OK;
}
static ShardResult mover_create(void* user, void* object)
{
    (void)user; (void)object;
    ++g_stats[S_CREATED];
    return SHARD_OK;
}
static ShardResult mover_play(void* user, void* object)
{
    Mover* m = (Mover*)object;
    void* other = 0;
    (void)user;
    ++g_stats[S_PLAYED];
    /* a text of the script, put in its blob through the pointer of the field */
    if (g_components) g_components->string_set(g_ctx, &m->label, S("hello"));
    /* a second script on the same actor : its object is in the module, written through the pointer given back */
    if (g_scripts) {
        if (g_scripts->get_object(g_ctx, m->owner, S("Counter"), &other) != SHARD_E_NOT_FOUND) ++g_stats[S_FAILURES];
        if (g_scripts->add_to_actor(g_ctx, m->owner, S("Counter"), &other) == SHARD_OK && other) {
            ((Counter*)other)->count = 7;
            ++g_stats[S_COUNTER_SET];
        }
    }
    return SHARD_OK;
}
static ShardResult mover_tick(void* user, void* object, float dt)
{
    Mover* m = (Mover*)object;
    ShardLocalTransform pose;
    float step = m->speed * dt;
    (void)user;
    ++m->ticks;
    ++m->secret;
    ++g_stats[S_TICKS];
    if (g_input && g_input->key_down((SHARD_KEY_A + 3))) step *= 2.0f;
    if (g_actors && g_actors->get_transform(m->owner, &pose) == SHARD_OK) {
        pose.position[0] += step;
        g_actors->set_transform(m->owner, &pose);
    }
    return SHARD_OK;
}
static ShardResult mover_destroyed(void* user, void* object)
{
    (void)user; (void)object;
    ++g_stats[S_DESTROYED];
    return SHARD_OK;
}
static ShardResult mover_contact(void* user, void* object, const ShardContact* contact)
{
    (void)user; (void)object; (void)contact;
    ++g_stats[S_CONTACTS];
    return SHARD_OK;
}

static ShardResult counter_construct(void* user, void* storage) { (void)user; ((Counter*)storage)->count = 0; return SHARD_OK; }
static void counter_destruct(void* user, void* storage) { (void)user; (void)storage; }

/* ---- the command ---- */
static ShardResult echo(void* user, ShardPluginCtx ctx, const ShardStr* args, uint32_t argCount, ShardStr* output)
{
    (void)user; (void)ctx;
    g_stats[S_COMMAND_ARGS] = (int)argCount;
    if (argCount == 0) return SHARD_E_INVALID_ARG;
    *output = args[0];
    return SHARD_OK;
}

/* ---- self tests : the host's answers, from inside ---- */
static ShardCVar g_speed, g_flag, g_count, g_name, g_dup;
static ShardBlob g_blob;

static ShardResult list_one(void* user, ShardStr path)
{
    (void)user;
    ++g_stats[S_LISTED];
    CHECK(equal(path, "hello.txt"));
    return SHARD_OK;
}

static void self_test(void)
{
    ShardStr text;
    ShardRandomState a, b, c;
    uint32_t size = 0, align = 0, count = 0;
    char buffer[64];
    uint64_t needed = 0;
    int32_t number = 0;
    float real = 0.0f;
    const void* table = 0;

    /* shard.plugin */
    CHECK(g_plugin != 0);
    text = g_plugin->id(g_ctx);
    CHECK(equal(text, "shard.test.wasm"));
    CHECK(equal(g_plugin->version(g_ctx), "1.0.0"));
    CHECK(equal(g_plugin->host(g_ctx), "headless"));
    CHECK(g_plugin->directory(g_ctx).size > 0);
    CHECK(g_plugin->has_permission(g_ctx, S("fs.read:content")) == SHARD_TRUE);
    CHECK(g_plugin->has_permission(g_ctx, S("physics")) == SHARD_FALSE);
    CHECK(g_plugin->random_stream(g_ctx, S("dice"), &a) == SHARD_OK);
    CHECK(g_plugin->random_stream(g_ctx, S("dice"), &b) == SHARD_OK);
    CHECK(g_plugin->random_stream(g_ctx, S("other"), &c) == SHARD_OK);
    CHECK(a.state == b.state && a.increment == b.increment);
    CHECK(a.state != c.state);

    /* what the host offers a sandboxed plugin, and what it does not */
    CHECK(g_host->query_interface(S("shard.cvars"), 1, 0, &table) == SHARD_OK && table != 0);
    CHECK(g_host->query_interface(S("shard.cvars"), 1, 9, &table) == SHARD_E_NOT_FOUND);       /* a revision it does not have */
    CHECK(g_host->query_interface(S("shard.editor"), 1, 0, &table) == SHARD_E_NOT_FOUND);
    CHECK(g_host->query_interface(S("shard.hooks"), 1, 0, &table) == SHARD_E_NOT_FOUND);
    CHECK(g_host->query_interface(S("shard.services"), 1, 0, &table) == SHARD_E_NOT_FOUND);
    CHECK(g_host->query_interface(S("shard.extensions"), 1, 0, &table) == SHARD_E_NOT_FOUND);
    CHECK(g_host->query_interface(S("shard.nothing"), 1, 0, &table) == SHARD_E_NOT_FOUND);
    CHECK(g_host->query_interface(S("shard.physics"), 1, 0, &table) == SHARD_E_NOT_FOUND);    /* not in the manifest's permissions */
    CHECK(g_host->query_interface(S("shard.audio"), 1, 0, &table) == SHARD_E_NOT_FOUND);
    CHECK(g_input != 0);                                                                     /* "input" is */

    /* shard.cvars */
    CHECK(g_cvars->create_bool(g_ctx, S("flag"), 1, S("a flag"), 0, &g_flag) == SHARD_OK);
    CHECK(g_cvars->create_int(g_ctx, S("count"), 5, S("a count"), 0, 0, 10, &g_count) == SHARD_OK);
    CHECK(g_cvars->create_string(g_ctx, S("name"), S("start"), S("a text"), 0, &g_name) == SHARD_OK);
    CHECK(g_cvars->create_int(g_ctx, S("count"), 5, S("again"), 0, 0, 10, &g_dup) == SHARD_E_ALREADY_EXISTS);
    CHECK(g_cvars->get_bool(g_ctx, g_flag, &number) == SHARD_OK && number == 1);
    CHECK(g_cvars->set_int(g_ctx, g_count, 99) == SHARD_OK);
    CHECK(g_cvars->get_int(g_ctx, g_count, &number) == SHARD_OK && number == 10);               /* clamped to its range */
    CHECK(g_cvars->get_float(g_ctx, g_count, &real) == SHARD_E_INVALID_ARG);                    /* another type */
    CHECK(g_cvars->get_int(g_ctx, 0x7777777700000001ull, &number) != SHARD_OK);                 /* never a handle of this plugin */
    CHECK(g_cvars->set_string(g_ctx, g_name, S("a longer text than the buffer")) == SHARD_OK);
    CHECK(g_cvars->get_string(g_ctx, g_name, buffer, 6, &needed) == SHARD_OK && needed == 29);  /* cut, and the whole length */
    CHECK(buffer[0] == 'a' && buffer[1] == ' ' && buffer[5] == 'g');
    CHECK(g_cvars->get_string(g_ctx, g_name, 0, 0, &needed) == SHARD_OK && needed == 29);       /* asking for the length */

    /* shard.content : its own folder */
    {
        char uri[128];
        uint64_t uriSize = 0, fileSize = 0;
        void* data = 0;
        char* bytes;
        CHECK(g_content->uri_of(g_ctx, S("hello.txt"), uri, sizeof(uri), &uriSize) == SHARD_OK);
        CHECK(uriSize == length("mod://shard.test.wasm/hello.txt") && memcmp(uri, "mod://shard.test.wasm/hello.txt", (size_t)uriSize) == 0);
        CHECK(g_content->read(g_ctx, S("mod://shard.test.wasm/hello.txt"), &data, &fileSize) == SHARD_OK && fileSize == 5);
        bytes = (char*)data;
        if (bytes && fileSize == 5) g_stats[S_HELLO_BYTES] = (bytes[0] == 'h') + (bytes[4] == 'o');
        g_host->free(g_ctx, data);
        CHECK(g_content->read(g_ctx, S("mod://com.other.mod/x.txt"), &data, &fileSize) == SHARD_E_INVALID_ARG);   /* only its own */
        CHECK(g_content->read(g_ctx, S("mod://shard.test.wasm/../x.txt"), &data, &fileSize) == SHARD_E_INVALID_ARG);
        CHECK(g_content->resolve(g_ctx, S("mod://shard.test.wasm/hello.txt"), uri, sizeof(uri), &uriSize) == SHARD_E_UNSUPPORTED);   /* a path of the host */
        CHECK(g_content->resolve_logical(g_ctx, S("hello.txt"), uri, sizeof(uri), &uriSize) == SHARD_E_UNSUPPORTED);
        CHECK(g_content->list(g_ctx, S(""), S(""), list_one, 0) == SHARD_OK);
    }

    /* shard.components */
    CHECK(g_components->find_type(g_ctx, S("shard.test.wasm.Spin"), &g_spinType) == SHARD_OK);
    CHECK(g_components->type_layout(g_ctx, g_spinType, &size, &align) == SHARD_OK && size == sizeof(Spin) && align == 8);
    CHECK(g_components->field_count(g_ctx, g_spinType, &count) == SHARD_OK && count == 4);
    {
        ShardFieldDesc field;
        memset(&field, 0, sizeof(field));
        field.structSize = sizeof(field);
        CHECK(g_components->field_at(g_ctx, g_spinType, 1, &field) == SHARD_OK);
        CHECK(equal(field.name, "speed") && field.type == SHARD_FIELD_F32 && field.offset == offsetof(Spin, speed) && equal(field.tooltip, "turns a second"));
        CHECK(g_components->find_field(g_ctx, g_spinType, S("note"), &field) == SHARD_OK && field.type == SHARD_FIELD_STRING);
        CHECK(g_components->find_field(g_ctx, g_spinType, S("nothing"), &field) == SHARD_E_NOT_FOUND);
    }
    CHECK(g_components->find_type(g_ctx, S("shard.LocalTransform"), &g_spinType) == SHARD_OK);   /* an engine's own */
    CHECK(g_components->find_type(g_ctx, S("shard.test.wasm.Spin"), &g_spinType) == SHARD_OK);

    /* blobs : the pointer is the module's copy, kept until it resizes or frees the blob */
    {
        ShardBlob blob = 0;
        void* data = 0;
        uint64_t blobSize = 0;
        unsigned char* bytes;
        int i;
        CHECK(g_components->blob_create(g_ctx, 8, &blob) == SHARD_OK && blob != 0);
        CHECK(g_components->blob_data(g_ctx, blob, &data, &blobSize) == SHARD_OK && blobSize == 8 && data != 0);
        bytes = (unsigned char*)data;
        for (i = 0; i < 8; ++i) bytes[i] = (unsigned char)(i + 1);
        CHECK(g_components->blob_resize(g_ctx, blob, 16) == SHARD_OK);                       /* the bytes it wrote are kept */
        CHECK(g_components->blob_data(g_ctx, blob, &data, &blobSize) == SHARD_OK && blobSize == 16);
        bytes = (unsigned char*)data;
        CHECK(bytes[0] == 1 && bytes[7] == 8 && bytes[8] == 0 && bytes[15] == 0);
        CHECK(g_components->blob_free(g_ctx, blob) == SHARD_OK);
        CHECK(g_components->blob_data(g_ctx, blob, &data, &blobSize) == SHARD_E_STALE_HANDLE);
    }
    {
        ShardBlob field = 0;
        ShardStr back;
        CHECK(g_components->string_set(g_ctx, &field, S("first")) == SHARD_OK && field != 0);
        CHECK(g_components->string_get(g_ctx, field, &back) == SHARD_OK && equal(back, "first"));
        CHECK(g_components->string_set(g_ctx, &field, S("and then a second")) == SHARD_OK);
        CHECK(g_components->string_get(g_ctx, field, &back) == SHARD_OK && equal(back, "and then a second"));
        CHECK(g_components->blob_free(g_ctx, field) == SHARD_OK);
    }

    /* shard.events : the handler runs while the module is inside emit */
    {
        int32_t one = 5;
        g_in_emit = 1;
        CHECK(g_events->emit(g_ctx, S("sandbox.ping"), &one, 4) == SHARD_OK);
        g_in_emit = 0;
        CHECK(g_stats[S_PING] >= 1 && g_stats[S_PING_VALUE] == 5);
        CHECK(g_stats[S_REENTRANT] >= 1);
        CHECK(g_events->unsubscribe(g_ctx, g_ping_subscription) == SHARD_OK);
        CHECK(g_events->unsubscribe(g_ctx, g_ping_subscription) == SHARD_E_NOT_FOUND);
    }
}

/* ---- the plugin ---- */
static ShardResult on_load(void* user, ShardPluginCtx ctx)
{
    ShardComponentDesc desc;
    ShardFieldDesc fields[4];
    Spin defaults;
    ShardSystemDesc system;
    ShardComponentType reads[1];
    ShardScriptDesc script;
    ShardScriptCallbacks callbacks;
    ShardFieldDesc moverFields[4];
    ShardFieldDesc counterFields[1];
    ShardScriptCallbacks counterCallbacks;
    ShardFieldDesc ping[1];
    (void)user;
    g_ctx = ctx;
    ++g_stats[S_LOAD];

    g_plugin = (const ShardPluginApi*)iface(SHARD_IFACE_PLUGIN, 2);
    g_cvars = (const ShardCVarsApi*)iface(SHARD_IFACE_CVARS, 0);
    g_commands = (const ShardCommandsApi*)iface(SHARD_IFACE_COMMANDS, 0);
    g_components = (const ShardComponentsApi*)iface(SHARD_IFACE_COMPONENTS, 3);
    g_ecs = (const ShardEcsApi*)iface(SHARD_IFACE_ECS, 1);
    g_systems = (const ShardSystemsApi*)iface(SHARD_IFACE_SYSTEMS, 0);
    g_events = (const ShardEventsApi*)iface(SHARD_IFACE_EVENTS, 1);
    g_content = (const ShardContentApi*)iface(SHARD_IFACE_CONTENT, 0);
    g_state = (const ShardStateApi*)iface(SHARD_IFACE_STATE, 0);
    g_actors = (const ShardActorsApi*)iface(SHARD_IFACE_ACTORS, 1);
    g_scripts = (const ShardScriptsApi*)iface(SHARD_IFACE_SCRIPTS, 1);
    g_input = (const ShardInputApi*)iface(SHARD_IFACE_INPUT, 0);
    if (!g_plugin || !g_cvars || !g_commands || !g_components || !g_ecs || !g_systems || !g_events || !g_content || !g_state || !g_actors || !g_scripts)
        return SHARD_E_NOT_FOUND;

    if (g_cvars->create_float(g_ctx, S("speed"), 2.5f, S("how fast"), 0, 0.0f, 10.0f, &g_speed) != SHARD_OK) return SHARD_E_INTERNAL;
    if (g_commands->add(g_ctx, S("sandbox.echo"), S("repeats a word"), echo, 0) != SHARD_OK) return SHARD_E_INTERNAL;

    /* a component */
    memset(fields, 0, sizeof(fields));
    fields[0].structSize = sizeof(ShardFieldDesc); fields[0].type = SHARD_FIELD_F32; fields[0].name = S("angle"); fields[0].offset = offsetof(Spin, angle); fields[0].flags = SHARD_FIELD_RUNTIME;
    fields[1].structSize = sizeof(ShardFieldDesc); fields[1].type = SHARD_FIELD_F32; fields[1].name = S("speed"); fields[1].offset = offsetof(Spin, speed);
    fields[1].min = 0.0f; fields[1].max = 20.0f; fields[1].tooltip = S("turns a second");
    fields[2].structSize = sizeof(ShardFieldDesc); fields[2].type = SHARD_FIELD_STRING; fields[2].name = S("note"); fields[2].offset = offsetof(Spin, note);
    fields[3].structSize = sizeof(ShardFieldDesc); fields[3].type = SHARD_FIELD_BLOB; fields[3].name = S("bulk"); fields[3].offset = offsetof(Spin, bulk);
    memset(&defaults, 0, sizeof(defaults));
    defaults.speed = 1.0f;
    memset(&desc, 0, sizeof(desc));
    desc.structSize = sizeof(desc);
    desc.size = sizeof(Spin);
    desc.align = 8;
    desc.fieldCount = 4;
    desc.name = S("Spin");
    desc.fields = fields;
    desc.defaults = &defaults;
    if (g_components->register_type(g_ctx, &desc, &g_spinType) != SHARD_OK) return SHARD_E_INTERNAL;

    /* its system */
    reads[0] = g_spinType;
    memset(&system, 0, sizeof(system));
    system.structSize = sizeof(system);
    system.phase = SHARD_PHASE_UPDATE;
    system.flags = 0;
    system.readCount = 1;
    system.writeCount = 1;
    system.name = S("spin");
    system.reads = reads;
    system.writes = reads;
    system.run = spin_run;
    if (g_systems->add(g_ctx, &system) != SHARD_OK) return SHARD_E_INTERNAL;

    /* an event, described and listened to */
    memset(ping, 0, sizeof(ping));
    ping[0].structSize = sizeof(ShardFieldDesc); ping[0].type = SHARD_FIELD_I32; ping[0].name = S("value"); ping[0].offset = 0;
    if (g_events->describe(g_ctx, S("sandbox.ping"), 4, ping, 1) != SHARD_OK) return SHARD_E_INTERNAL;
    if (g_events->subscribe(g_ctx, S("sandbox.ping"), on_ping, 0, &g_ping_subscription) != SHARD_OK) return SHARD_E_INTERNAL;

    /* two scripts */
    memset(moverFields, 0, sizeof(moverFields));
    moverFields[0].structSize = sizeof(ShardFieldDesc); moverFields[0].type = SHARD_FIELD_F32; moverFields[0].name = S("speed"); moverFields[0].offset = offsetof(Mover, speed);
    moverFields[1].structSize = sizeof(ShardFieldDesc); moverFields[1].type = SHARD_FIELD_STRING; moverFields[1].name = S("label"); moverFields[1].offset = offsetof(Mover, label);
    moverFields[2].structSize = sizeof(ShardFieldDesc); moverFields[2].type = SHARD_FIELD_ASSET; moverFields[2].name = S("icon"); moverFields[2].offset = offsetof(Mover, icon);
    moverFields[3].structSize = sizeof(ShardFieldDesc); moverFields[3].type = SHARD_FIELD_I32; moverFields[3].name = S("ticks"); moverFields[3].offset = offsetof(Mover, ticks);
    moverFields[3].flags = SHARD_FIELD_RUNTIME;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.structSize = sizeof(callbacks);
    callbacks.construct = mover_construct;
    callbacks.destruct = mover_destruct;
    callbacks.attach = mover_attach;
    callbacks.on_create = mover_create;
    callbacks.on_play = mover_play;
    callbacks.on_tick = mover_tick;
    callbacks.on_destroyed = mover_destroyed;
    callbacks.on_contact_added = mover_contact;
    memset(&script, 0, sizeof(script));
    script.structSize = sizeof(script);
    script.size = sizeof(Mover);
    script.align = 8;
    script.fieldCount = 4;
    script.name = S("Mover");
    script.fields = moverFields;
    script.user = 0;
    script.callbacks = &callbacks;
    if (g_scripts->register_script(g_ctx, &script) != SHARD_OK) return SHARD_E_INTERNAL;

    memset(counterFields, 0, sizeof(counterFields));
    counterFields[0].structSize = sizeof(ShardFieldDesc); counterFields[0].type = SHARD_FIELD_I32; counterFields[0].name = S("count"); counterFields[0].offset = 0;
    memset(&counterCallbacks, 0, sizeof(counterCallbacks));
    counterCallbacks.structSize = sizeof(counterCallbacks);
    counterCallbacks.construct = counter_construct;
    counterCallbacks.destruct = counter_destruct;
    memset(&script, 0, sizeof(script));
    script.structSize = sizeof(script);
    script.size = sizeof(Counter);
    script.align = 4;
    script.fieldCount = 1;
    script.name = S("Counter");
    script.fields = counterFields;
    script.callbacks = &counterCallbacks;
    if (g_scripts->register_script(g_ctx, &script) != SHARD_OK) return SHARD_E_INTERNAL;
    return SHARD_OK;
}

static ShardResult on_initialize(void* user, ShardPluginCtx ctx) { (void)user; (void)ctx; ++g_stats[S_INITIALIZE]; return SHARD_OK; }

static ShardResult on_enable(void* user, ShardPluginCtx ctx)
{
    float v = 0.0f;
    (void)user; (void)ctx;
    ++g_stats[S_ENABLE];
    self_test();
    if (g_stats[S_FAILURES]) return SHARD_E_INTERNAL;
    g_cvars->get_float(g_ctx, g_speed, &v);
    return g_cvars->set_float(g_ctx, g_speed, v + 1.0f);
}
static ShardResult on_disable(void* user, ShardPluginCtx ctx) { (void)user; (void)ctx; ++g_stats[S_DISABLE]; return SHARD_OK; }
static ShardResult on_frame_begin(void* user, ShardPluginCtx ctx, uint64_t frame) { (void)user; (void)ctx; (void)frame; ++g_stats[S_FRAME_BEGIN]; return SHARD_OK; }
static ShardResult on_frame_end(void* user, ShardPluginCtx ctx, uint64_t frame) { (void)user; (void)ctx; (void)frame; ++g_stats[S_FRAME_END]; return SHARD_OK; }

/* Hot reload : a number written before, read back in the new module */
static ShardResult on_prepare_reload(void* user, ShardPluginCtx ctx, ShardState state)
{
    (void)user; (void)ctx;
    return g_state->set(state, S("counter"), S("{\"ticks\": 42, \"text\": \"kept\"}"));
}
static ShardResult on_restore(void* user, ShardPluginCtx ctx, ShardState state)
{
    char buffer[64];
    uint64_t size = 0;
    (void)user; (void)ctx;
    if (!g_state->has(state, S("counter"))) return SHARD_E_NOT_FOUND;
    if (g_state->get(state, S("counter"), buffer, sizeof(buffer), &size) != SHARD_OK) return SHARD_E_NOT_FOUND;
    {
        uint64_t i;
        for (i = 0; i + 1 < size && i + 1 < sizeof(buffer); ++i) if (buffer[i] == '4' && buffer[i + 1] == '2') g_stats[S_RESTORED] = 42;
    }
    return g_state->has(state, S("nothing")) == SHARD_FALSE ? SHARD_OK : SHARD_E_INTERNAL;
}

static ShardResult on_world_loaded(void* user, ShardPluginCtx ctx, const ShardWorldInfo* world)
{
    (void)user; (void)ctx;
    if (world && world->name.size > 0) ++g_stats[S_WORLD_LOADED];
    return SHARD_OK;
}

/* ---- what the test calls ---- */
EXPORT("wasm_make_blob") uint64_t wasm_make_blob(void)
{
    void* data = 0;
    uint64_t size = 0;
    unsigned char* bytes;
    int i;
    if (g_components->blob_create(g_ctx, 6, &g_blob) != SHARD_OK) return 0;
    if (g_components->blob_data(g_ctx, g_blob, &data, &size) != SHARD_OK) return 0;
    bytes = (unsigned char*)data;
    for (i = 0; i < 6; ++i) bytes[i] = (unsigned char)(10 * (i + 1));
    return g_blob;
}
EXPORT("wasm_spin_type") uint64_t wasm_spin_type(void) { return g_spinType; }
EXPORT("wasm_ping") int wasm_ping(int value)
{
    int32_t v = value;
    g_in_emit = 1;
    if (g_events->subscribe(g_ctx, S("sandbox.ping"), on_ping, 0, &g_ping_subscription) != SHARD_OK) return -1;
    g_events->emit(g_ctx, S("sandbox.ping"), &v, 4);
    g_in_emit = 0;
    return g_stats[S_PING_VALUE];
}
EXPORT("wasm_speed") float wasm_speed(void)
{
    float v = 0.0f;
    g_cvars->get_float(g_ctx, g_speed, &v);
    return v;
}
EXPORT("wasm_loop") void wasm_loop(void) { volatile int i = 0; for (;;) ++i; }
EXPORT("wasm_oob") int wasm_oob(void) { volatile int* p = (volatile int*)0x7FFFFFF0; return *p; }

SHARD_PLUGIN_EXPORT ShardResult SHARD_CALL shard_plugin_entry(const ShardHostApi* host, ShardPluginInfo* out)
{
    static ShardPluginCallbacks callbacks;
    g_host = host;
    callbacks.structSize = sizeof(ShardPluginCallbacks);
    callbacks.on_load = on_load;
    callbacks.on_initialize = on_initialize;
    callbacks.on_enable = on_enable;
    callbacks.on_disable = on_disable;
    callbacks.on_prepare_reload = on_prepare_reload;
    callbacks.on_restore = on_restore;
    callbacks.on_frame_begin = on_frame_begin;
    callbacks.on_frame_end = on_frame_end;
    callbacks.on_world_loaded = on_world_loaded;
    out->structSize = sizeof(ShardPluginInfo);
    out->abiMajor = SHARD_ABI_MAJOR;
    out->abiMinor = SHARD_ABI_MINOR;
    out->user = (void*)0x1234;
    out->callbacks = &callbacks;
    return SHARD_OK;
}
