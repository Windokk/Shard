// Tier 2 (abi wasm1) C plugin: Clang for wasm32, no libc, wasm-ld into test_plugins/shard.test.wasm. SHARD_WASM_CHECK_LAYOUT asserts the wasm32 layouts the host reads.
#define SHARD_WASM_CHECK_LAYOUT
#include <shard/shard.h>

#include <stddef.h>

#include "wasm_stats.h"

#define EXPORT(name) __attribute__((export_name(name)))

// No libc: what Clang may call for a struct copy, plus a few string helpers.
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

typedef struct Mover {
    float speed;
    ShardBlob label;
    int32_t icon;
    int32_t ticks;
    ShardActor owner;
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
    if (g_components) g_components->string_set(g_ctx, &m->label, S("hello"));
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

static ShardResult echo(void* user, ShardPluginCtx ctx, const ShardStr* args, uint32_t argCount, ShardStr* output)
{
    (void)user; (void)ctx;
    g_stats[S_COMMAND_ARGS] = (int)argCount;
    if (argCount == 0) return SHARD_E_INVALID_ARG;
    *output = args[0];
    return SHARD_OK;
}

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

    CHECK(g_host->query_interface(S("shard.cvars"), 1, 0, &table) == SHARD_OK && table != 0);
    CHECK(g_host->query_interface(S("shard.cvars"), 1, 9, &table) == SHARD_E_NOT_FOUND);
    CHECK(g_host->query_interface(S("shard.editor"), 1, 0, &table) == SHARD_E_NOT_FOUND);
    CHECK(g_host->query_interface(S("shard.hooks"), 1, 0, &table) == SHARD_E_NOT_FOUND);
    CHECK(g_host->query_interface(S("shard.services"), 1, 0, &table) == SHARD_E_NOT_FOUND);
    CHECK(g_host->query_interface(S("shard.extensions"), 1, 0, &table) == SHARD_E_NOT_FOUND);
    CHECK(g_host->query_interface(S("shard.nothing"), 1, 0, &table) == SHARD_E_NOT_FOUND);
    CHECK(g_host->query_interface(S("shard.physics"), 1, 0, &table) == SHARD_E_NOT_FOUND);
    CHECK(g_host->query_interface(S("shard.audio"), 1, 0, &table) == SHARD_E_NOT_FOUND);
    CHECK(g_input != 0);

    CHECK(g_cvars->create_bool(g_ctx, S("flag"), 1, S("a flag"), 0, &g_flag) == SHARD_OK);
    CHECK(g_cvars->create_int(g_ctx, S("count"), 5, S("a count"), 0, 0, 10, &g_count) == SHARD_OK);
    CHECK(g_cvars->create_string(g_ctx, S("name"), S("start"), S("a text"), 0, &g_name) == SHARD_OK);
    CHECK(g_cvars->create_int(g_ctx, S("count"), 5, S("again"), 0, 0, 10, &g_dup) == SHARD_E_ALREADY_EXISTS);
    CHECK(g_cvars->get_bool(g_ctx, g_flag, &number) == SHARD_OK && number == 1);
    CHECK(g_cvars->set_int(g_ctx, g_count, 99) == SHARD_OK);
    CHECK(g_cvars->get_int(g_ctx, g_count, &number) == SHARD_OK && number == 10);
    CHECK(g_cvars->get_float(g_ctx, g_count, &real) == SHARD_E_INVALID_ARG);
    CHECK(g_cvars->get_int(g_ctx, 0x7777777700000001ull, &number) != SHARD_OK);
    CHECK(g_cvars->set_string(g_ctx, g_name, S("a longer text than the buffer")) == SHARD_OK);
    CHECK(g_cvars->get_string(g_ctx, g_name, buffer, 6, &needed) == SHARD_OK && needed == 29);
    CHECK(buffer[0] == 'a' && buffer[1] == ' ' && buffer[5] == 'g');
    CHECK(g_cvars->get_string(g_ctx, g_name, 0, 0, &needed) == SHARD_OK && needed == 29);

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
        CHECK(g_content->read(g_ctx, S("mod://com.other.mod/x.txt"), &data, &fileSize) == SHARD_E_INVALID_ARG);
        CHECK(g_content->read(g_ctx, S("mod://shard.test.wasm/../x.txt"), &data, &fileSize) == SHARD_E_INVALID_ARG);
        CHECK(g_content->resolve(g_ctx, S("mod://shard.test.wasm/hello.txt"), uri, sizeof(uri), &uriSize) == SHARD_E_UNSUPPORTED);
        CHECK(g_content->resolve_logical(g_ctx, S("hello.txt"), uri, sizeof(uri), &uriSize) == SHARD_E_UNSUPPORTED);
        CHECK(g_content->list(g_ctx, S(""), S(""), list_one, 0) == SHARD_OK);
    }

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
    CHECK(g_components->find_type(g_ctx, S("shard.LocalTransform"), &g_spinType) == SHARD_OK);
    CHECK(g_components->find_type(g_ctx, S("shard.test.wasm.Spin"), &g_spinType) == SHARD_OK);

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
        CHECK(g_components->blob_resize(g_ctx, blob, 16) == SHARD_OK);
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

    memset(ping, 0, sizeof(ping));
    ping[0].structSize = sizeof(ShardFieldDesc); ping[0].type = SHARD_FIELD_I32; ping[0].name = S("value"); ping[0].offset = 0;
    if (g_events->describe(g_ctx, S("sandbox.ping"), 4, ping, 1) != SHARD_OK) return SHARD_E_INTERNAL;
    if (g_events->subscribe(g_ctx, S("sandbox.ping"), on_ping, 0, &g_ping_subscription) != SHARD_OK) return SHARD_E_INTERNAL;

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

// shard.timers : callbacks the host reaches through the module's function table
static const ShardTimersApi* g_timers;
static ShardTimer g_every;
static ShardLatch g_latch;
static int g_coroState;

static void on_once(void* user) { CHECK(user == (void*)&g_coroState); ++g_stats[S_TIMER_ONCE]; }
static void on_every(void* user) { (void)user; ++g_stats[S_TIMER_EVERY]; }
static void on_step(void* user, ShardWait* wait)
{
    (void)user;
    CHECK(wait->kind == SHARD_WAIT_DONE);
    switch (g_coroState++) {
        case 0: ++g_stats[S_CORO_STEPS]; wait->kind = SHARD_WAIT_SECONDS; wait->seconds = 1.0; break;
        case 1: ++g_stats[S_CORO_STEPS]; wait->kind = SHARD_WAIT_LATCH; wait->latch = g_latch; wait->seconds = 3.0; break;
        default: ++g_stats[S_CORO_DONE]; g_stats[S_LATCH_FIRED] = g_timers->latch_fired(g_ctx, g_latch); break;
    }
}

EXPORT("wasm_timers_start") int wasm_timers_start(void)
{
    ShardTimer once = 0, coroutine = 0;
    g_timers = (const ShardTimersApi*)iface(SHARD_IFACE_TIMERS, 0);
    if (!g_timers) return -100;
    g_coroState = 0;
    if (g_timers->event_latch(g_ctx, S("wasm.go"), &g_latch) != SHARD_OK) return -101;
    if (g_timers->after(g_ctx, 0.5, on_once, (void*)&g_coroState, &once) != SHARD_OK || !once) return -102;
    if (g_timers->every(g_ctx, 0.25, on_every, 0, &g_every) != SHARD_OK || !g_every) return -103;
    if (g_timers->start(g_ctx, on_step, 0, &coroutine) != SHARD_OK || !coroutine) return -104;
    CHECK(g_timers->is_active(g_ctx, once) == SHARD_TRUE);
    return SHARD_OK;
}
EXPORT("wasm_timers_cancel_every") int wasm_timers_cancel_every(void) { return g_timers->cancel(g_ctx, g_every); }
EXPORT("wasm_timers_cancel_all") int wasm_timers_cancel_all(void) { return g_timers->cancel_all(g_ctx); }
EXPORT("wasm_timers_active") int wasm_timers_active(void) { return g_timers->is_active(g_ctx, g_every); }

static const ShardTimeApi* g_time;
static ShardTimeInfo g_time_info;
static uint64_t g_hash_total;

EXPORT("wasm_time_probe") int wasm_time_probe(void)
{
    g_time = (const ShardTimeApi*)iface(SHARD_IFACE_TIME, 0);
    if (!g_time) return -100;
    g_time_info.structSize = sizeof(g_time_info);
    return g_time->get(&g_time_info);
}
EXPORT("wasm_time_real_is_sim") int wasm_time_real_is_sim(void) { return g_time_info.realTime == g_time_info.simTime; }
EXPORT("wasm_time_sim_us") uint64_t wasm_time_sim_us(void) { return (uint64_t)(g_time_info.simTime * 1e6 + 0.5); }
EXPORT("wasm_time_fixed_step") uint64_t wasm_time_fixed_step(void) { return g_time_info.fixedStep; }
EXPORT("wasm_time_fixed_dt_us") int wasm_time_fixed_dt_us(void) { return (int)(g_time_info.fixedDeltaTime * 1e6f + 0.5f); }
EXPORT("wasm_time_scale_milli") int wasm_time_scale_milli(void) { return (int)(g_time_info.timeScale * 1000.0f + 0.5f); }
EXPORT("wasm_time_set_scale") int wasm_time_set_scale(int milli) { return g_time->set_time_scale((float)milli / 1000.0f); }
EXPORT("wasm_time_set_fixed_us") int wasm_time_set_fixed_us(int micros) { return g_time->set_fixed_delta_time((float)micros / 1e6f); }
EXPORT("wasm_time_hash") int wasm_time_hash(void)
{
    ShardStateHash hash;
    ShardResult result;
    hash.structSize = sizeof(hash);
    result = g_time->state_hash(&hash);
    g_hash_total = hash.total;
    return result;
}
EXPORT("wasm_time_hash_total") uint64_t wasm_time_hash_total(void) { return g_hash_total; }

static const ShardJobsApi* g_jobs;
static ShardJob g_job_a, g_job_b, g_job_c, g_job_self;
static int g_job_log[64];
static int g_job_log_count;
static int g_slots[100];

static void job_note(void* user, uint32_t worker)
{
    if (worker != 0) ++g_stats[S_JOB_WORKER_BAD];
    if (g_job_log_count < 64) g_job_log[g_job_log_count++] = (int)(size_t)user;
    ++g_stats[S_JOB_RUNS];
}
static void job_wait_self(void* user, uint32_t worker)
{
    (void)user; (void)worker;
    g_stats[S_JOB_SELF_WAIT] = g_jobs->wait(g_ctx, g_job_self);
    ++g_stats[S_JOB_RUNS];
}
static void on_post(void* user) { g_stats[S_POST_VALUE] = (int)(size_t)user; ++g_stats[S_POST_RUNS]; }
static void job_post(void* user, uint32_t worker)
{
    (void)worker;
    if (g_jobs->post_main(g_ctx, on_post, user) != SHARD_OK) ++g_stats[S_FAILURES];
    ++g_stats[S_JOB_RUNS];
}
static void job_spin(void* user, uint32_t worker) { volatile int i = 0; (void)user; (void)worker; for (;;) ++i; }
static void range_note(void* user, uint32_t begin, uint32_t end, uint32_t worker)
{
    uint32_t i;
    int* slots = (int*)user;
    if (worker != 0) ++g_stats[S_JOB_WORKER_BAD];
    ++g_stats[S_RANGE_CALLS];
    for (i = begin; i < end; ++i) slots[i] += (int)i + 1;
}

EXPORT("wasm_jobs_open") int wasm_jobs_open(void)
{
    g_jobs = (const ShardJobsApi*)iface(SHARD_IFACE_JOBS, 0);
    return g_jobs ? SHARD_OK : -100;
}
EXPORT("wasm_jobs_workers") int wasm_jobs_workers(void) { return (int)g_jobs->worker_count(); }
EXPORT("wasm_jobs_submit") int wasm_jobs_submit(void)
{
    g_job_log_count = 0;
    if (g_jobs->run(g_ctx, job_note, (void*)1, &g_job_a) != SHARD_OK || !g_job_a) return -1;
    if (g_jobs->run(g_ctx, job_note, (void*)2, &g_job_b) != SHARD_OK || !g_job_b) return -2;
    if (g_jobs->run(g_ctx, job_note, (void*)3, 0) != SHARD_OK) return -3;
    return SHARD_OK;
}
EXPORT("wasm_jobs_run_null") int wasm_jobs_run_null(void) { return g_jobs->run(g_ctx, 0, 0, 0); }
EXPORT("wasm_jobs_wait_a") int wasm_jobs_wait_a(void) { return g_jobs->wait(g_ctx, g_job_a); }
EXPORT("wasm_jobs_wait_b") int wasm_jobs_wait_b(void) { return g_jobs->wait(g_ctx, g_job_b); }
EXPORT("wasm_jobs_is_done_a") int wasm_jobs_is_done_a(void) { return g_jobs->is_done(g_ctx, g_job_a); }
EXPORT("wasm_jobs_release_b") int wasm_jobs_release_b(void) { return g_jobs->release(g_ctx, g_job_b); }
EXPORT("wasm_jobs_log_count") int wasm_jobs_log_count(void) { return g_job_log_count; }
EXPORT("wasm_jobs_log") int wasm_jobs_log(int i) { return i >= 0 && i < g_job_log_count ? g_job_log[i] : -1; }
EXPORT("wasm_jobs_submit_self") int wasm_jobs_submit_self(void) { return g_jobs->run(g_ctx, job_wait_self, 0, &g_job_self); }
EXPORT("wasm_jobs_wait_self") int wasm_jobs_wait_self(void) { return g_jobs->wait(g_ctx, g_job_self); }
EXPORT("wasm_jobs_submit_post") int wasm_jobs_submit_post(int value) { return g_jobs->run(g_ctx, job_post, (void*)(size_t)value, 0); }
EXPORT("wasm_jobs_post") int wasm_jobs_post(int value) { return g_jobs->post_main(g_ctx, on_post, (void*)(size_t)value); }
EXPORT("wasm_jobs_submit_spin") int wasm_jobs_submit_spin(void) { return g_jobs->run(g_ctx, job_spin, 0, 0); }
EXPORT("wasm_jobs_parallel_for") int wasm_jobs_parallel_for(int count, int grain)
{
    int i;
    for (i = 0; i < 100; ++i) g_slots[i] = 0;
    g_stats[S_RANGE_CALLS] = 0;
    return g_jobs->parallel_for(g_ctx, (uint32_t)count, (uint32_t)grain, range_note, g_slots);
}
EXPORT("wasm_jobs_parallel_async") int wasm_jobs_parallel_async(int count, int grain)
{
    int i;
    for (i = 0; i < 100; ++i) g_slots[i] = 0;
    g_stats[S_RANGE_CALLS] = 0;
    return g_jobs->parallel_for_async(g_ctx, (uint32_t)count, (uint32_t)grain, range_note, g_slots, &g_job_c);
}
EXPORT("wasm_jobs_parallel_async_no_out") int wasm_jobs_parallel_async_no_out(void) { return g_jobs->parallel_for_async(g_ctx, 4, 1, range_note, g_slots, 0); }
EXPORT("wasm_jobs_wait_c") int wasm_jobs_wait_c(void) { return g_jobs->wait(g_ctx, g_job_c); }
EXPORT("wasm_jobs_slot_sum") int wasm_jobs_slot_sum(void)
{
    int i, sum = 0;
    for (i = 0; i < 100; ++i) sum += g_slots[i];
    return sum;
}

static const ShardAssetsApi* g_assets;
static uint8_t g_pixels[16];
static ShardResult on_asset(void* user, const ShardAssetInfo* asset)
{
    (void)user;
    if (asset->structSize < sizeof(ShardAssetInfo) || asset->path.size == 0 || asset->sizeBytes == 0) ++g_stats[S_ASSET_LIST_BAD];
    ++g_stats[S_ASSET_LISTED];
    return SHARD_OK;
}

EXPORT("wasm_assets_open") int wasm_assets_open(void)
{
    g_assets = (const ShardAssetsApi*)iface(SHARD_IFACE_ASSETS, 0);
    return g_assets ? SHARD_OK : -100;
}
EXPORT("wasm_assets_probe") int wasm_assets_probe(void)
{
    int32_t id = 0, missing = 7, picture = 0;
    ShardAssetInfo info;
    ShardMeshInfo meshInfo;
    ShardMeshRead read;
    float positions[12];
    char buffer[64];
    uint64_t size = 0, bytes = 0;
    void* data = 0;
    uint32_t width = 0, height = 0;
    memset(&info, 0, sizeof(info));
    memset(&meshInfo, 0, sizeof(meshInfo));
    memset(&read, 0, sizeof(read));
    CHECK(g_assets->resolve(S("data/hello.txt"), &id) == SHARD_OK && id != 0);
    CHECK(g_assets->resolve(S("data/none.txt"), &missing) == SHARD_E_NOT_FOUND && missing == 0);
    info.structSize = sizeof(info);
    CHECK(g_assets->info(id, &info) == SHARD_OK);
    CHECK(info.id == id && info.sizeBytes == 12 && equal(info.path, "data/hello.txt") && info.typeName.size == 0);
    CHECK(g_assets->info(123456, &info) == SHARD_E_NOT_FOUND);
    CHECK(g_assets->path_of(id, buffer, sizeof(buffer), &size) == SHARD_OK && size == 14 && memcmp(buffer, "data/hello.txt", 14) == 0);
    CHECK(g_assets->exists(S("data/hello.txt")) == SHARD_TRUE);
    CHECK(g_assets->exists(S("data/none.txt")) == SHARD_FALSE);
    CHECK(g_assets->exists(S("../outside.txt")) == SHARD_FALSE);
    CHECK(g_assets->exists(S("/data/hello.txt")) == SHARD_FALSE);
    CHECK(g_assets->exists(S("C:/Windows/win.ini")) == SHARD_FALSE);
    CHECK(g_assets->read(g_ctx, S("data/hello.txt"), &data, &bytes) == SHARD_OK && bytes == 12 && memcmp(data, "hello assets", 12) == 0);
    g_host->free(g_ctx, data);
    data = 0;
    CHECK(g_assets->read(g_ctx, S("data/none.txt"), &data, &bytes) == SHARD_E_NOT_FOUND);
    CHECK(g_assets->read(g_ctx, S("../secret.txt"), &data, &bytes) == SHARD_E_INVALID_ARG);
    CHECK(g_assets->read(g_ctx, S("/data/hello.txt"), &data, &bytes) == SHARD_E_INVALID_ARG);
    CHECK(g_assets->read(g_ctx, S("C:/Windows/win.ini"), &data, &bytes) == SHARD_E_INVALID_ARG);
    CHECK(g_assets->read(g_ctx, S("mod://shard.other/hello.txt"), &data, &bytes) == SHARD_E_INVALID_ARG);
    CHECK(g_assets->list(S("data"), SHARD_ASSET_UNKNOWN, on_asset, 0) == SHARD_OK);
    CHECK(g_assets->list(S("../data"), SHARD_ASSET_UNKNOWN, on_asset, 0) == SHARD_E_INVALID_ARG);
    CHECK(g_assets->list(S("data"), SHARD_ASSET_MESH, on_asset, 0) == SHARD_OK);
    CHECK(g_stats[S_ASSET_LISTED] == 2 && g_stats[S_ASSET_LIST_BAD] == 0);

    meshInfo.structSize = sizeof(meshInfo);
    CHECK(g_assets->mesh_info(id, &meshInfo) == SHARD_E_NOT_FOUND);
    read.structSize = sizeof(read);
    read.vertexCapacity = 4;
    read.positions = positions;
    CHECK(g_assets->mesh_read(id, &read) == SHARD_E_NOT_FOUND && read.vertexCount == 0);
    CHECK(g_assets->image_info(id, &width, &height) == SHARD_E_NOT_FOUND);

    CHECK(g_assets->resolve(S("data/pic.png"), &picture) == SHARD_OK);
    CHECK(g_assets->image_info(picture, &width, &height) == SHARD_OK && width == 2 && height == 2);
    CHECK(g_assets->image_read(picture, 0, 0, &size) == SHARD_OK && size == 16);
    size = 0;
    CHECK(g_assets->image_read(picture, g_pixels, sizeof(g_pixels), &size) == SHARD_OK && size == 16);
    return g_stats[S_FAILURES];
}
EXPORT("wasm_assets_pixel") int wasm_assets_pixel(int i) { return i >= 0 && i < 16 ? g_pixels[i] : -1; }
EXPORT("wasm_assets_write") int wasm_assets_write(void)
{
    int32_t id = 0;
    const char text[] = "xyz";
    return g_assets->write(g_ctx, S("out/baked.bin"), text, 3, &id) == SHARD_OK && id != 0 ? SHARD_OK : -1;
}
EXPORT("wasm_assets_write_no_id") int wasm_assets_write_no_id(void) { return g_assets->write(g_ctx, S("out/noid.bin"), "q", 1, 0); }
EXPORT("wasm_assets_write_escape") int wasm_assets_write_escape(void) { return g_assets->write(g_ctx, S("../escaped.bin"), "x", 1, 0); }
EXPORT("wasm_assets_write_absolute") int wasm_assets_write_absolute(void) { return g_assets->write(g_ctx, S("C:/escaped.bin"), "x", 1, 0); }
EXPORT("wasm_assets_mesh_oob") int wasm_assets_mesh_oob(void)
{
    ShardMeshRead read;
    memset(&read, 0, sizeof(read));
    read.structSize = sizeof(read);
    read.vertexCapacity = 1000000;
    read.positions = (float*)0x7FFFFFF0u;
    return g_assets->mesh_read(1, &read);
}

static const ShardDebugApi* g_debug;
EXPORT("wasm_debug_open") int wasm_debug_open(void)
{
    g_debug = (const ShardDebugApi*)iface(SHARD_IFACE_DEBUG, 0);
    return g_debug ? SHARD_OK : -100;
}
EXPORT("wasm_debug_probe") int wasm_debug_probe(void)
{
    const float origin[3] = { 0.0f, 0.0f, 0.0f }, unit[3] = { 1.0f, 2.0f, 3.0f }, up[3] = { 0.0f, 1.0f, 0.0f }, rotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    const float points[12] = { 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0 };
    const float triangle[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    const uint32_t red = 0xFF0000FFu;
    int enabled = g_debug->enabled();
    g_debug->line(origin, unit, red, 0.0f, SHARD_DEBUG_DEPTH_TEST);
    g_debug->lines(points, 4, red, 0.0f, 0);
    g_debug->point(unit, 0.1f, red, 0.0f, 0);
    g_debug->arrow(origin, unit, red, 0.0f, 0);
    g_debug->circle(origin, up, 1.0f, red, 0.0f, 0);
    g_debug->sphere(origin, 1.0f, red, 0.0f, 0);
    g_debug->box(origin, unit, 0, red, 0.0f, 0);
    g_debug->box(origin, unit, rotation, red, 0.0f, 0);
    g_debug->capsule(origin, unit, 0.5f, red, 0.0f, 0);
    g_debug->cone(origin, up, 30.0f, red, 0.0f, 0);
    g_debug->triangles(triangle, 1, red, 0.0f, 0);
    g_debug->text(origin, S("a label"), red, 0.0f, 0);
    g_debug->text_screen(10.0f, 10.0f, S("on the screen"), red, 0.0f);
    g_debug->lines(points, 0, red, 0.0f, 0);
    g_debug->clear();
    return enabled == SHARD_TRUE || enabled == SHARD_FALSE ? SHARD_OK : -1;
}
EXPORT("wasm_debug_oob") void wasm_debug_oob(void) { g_debug->lines((const float*)0x7FFFFFF0u, 1000, 0, 0.0f, 0); }

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
