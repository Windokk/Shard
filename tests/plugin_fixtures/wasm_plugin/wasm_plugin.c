/* A sandboxed plugin in C : what WasmPluginTest builds byte by byte (a cvar, a command, its content), as a plugin author makes it. Compiled by
 * Clang and linked by wasm-ld into test_plugins/shard.test.wasm (tests/CMakeLists.txt), run by WasmSdkPluginTest ; Wasm.SdkSignatures checks
 * the signatures Clang gives its imports and callbacks against the ones the host links. */
#include <shard/shard_host.h>
#include <shard/shard_wasm.h>

#include <stddef.h>

/* The wasm32 layouts the host reads (src/engine/plugins/wasm/wasm_plugin.cpp) : a change here is a change of the sandbox's ABI */
_Static_assert(sizeof(void*) == 4, "wasm32");
_Static_assert(sizeof(ShardStr) == 16 && offsetof(ShardStr, size) == 8, "ShardStr");
_Static_assert(sizeof(ShardPluginInfo) == 40 && offsetof(ShardPluginInfo, id) == 16 && offsetof(ShardPluginInfo, user) == 32 &&
               offsetof(ShardPluginInfo, callbacks) == 36, "ShardPluginInfo");
_Static_assert(offsetof(ShardPluginCallbacks, on_load) == 4 && offsetof(ShardPluginCallbacks, on_unload) == 20 &&
               offsetof(ShardPluginCallbacks, on_frame_begin) == 32 && offsetof(ShardPluginCallbacks, on_frame_end) == 36, "ShardPluginCallbacks");

static ShardCVar g_speed;
static ShardResult echo(void* user, ShardPluginCtx ctx, const ShardStr* args, uint32_t argCount, ShardStr* output)
{
    (void)user; (void)ctx;
    if (argCount == 0) return SHARD_E_INVALID_ARG;
    *output = args[0];
    return SHARD_OK;
}
static ShardResult on_load(void* user, ShardPluginCtx ctx)
{
    (void)user;
    ShardStr msg = { "hello", 5 }, name = { "speed", 5 }, help = { "how fast", 8 };
    shard_wasm_log(ctx, SHARD_LOG_INFO, msg);
    ShardStr command = { "sandbox.echo", 12 }, commandHelp = { "repeats a word", 14 };
    if (shard_wasm_command_add(ctx, command, commandHelp, echo, 0) != SHARD_OK) return SHARD_E_INTERNAL;
    return shard_wasm_cvar_create_float(ctx, name, 2.5f, help, 0, 0.f, 10.f, &g_speed);
}
static ShardResult on_enable(void* user, ShardPluginCtx ctx)
{
    (void)user;
    float v = 0.f;
    char text[64];
    uint64_t size = 0;
    ShardStr file = { "hello.txt", 9 };
    if (shard_wasm_content_read(ctx, file, text, sizeof(text), &size) != SHARD_OK) return SHARD_E_NOT_FOUND;
    shard_wasm_cvar_get_float(ctx, g_speed, &v);
    return shard_wasm_cvar_set_float(ctx, g_speed, v + 1.f);
}
static ShardPluginCallbacks g_callbacks;
SHARD_PLUGIN_EXPORT ShardResult SHARD_CALL shard_plugin_entry(const ShardHostApi* host, ShardPluginInfo* out)
{
    (void)host;
    g_callbacks.structSize = sizeof(ShardPluginCallbacks);
    g_callbacks.on_load = on_load;
    g_callbacks.on_enable = on_enable;
    out->structSize = sizeof(ShardPluginInfo);
    out->abiMajor = SHARD_ABI_MAJOR;
    out->abiMinor = SHARD_ABI_MINOR;
    out->callbacks = &g_callbacks;
    return SHARD_OK;
}
