#include <gtest/gtest.h>

#include <cstddef>

#include <shard/shard_abi.h>
#include <shard/shard_commands.h>
#include <shard/shard_cvars.h>
#include <shard/shard_host.h>
#include <shard/shard_plugin.h>

// Pins the layout of every struct of sdk/include/shard. A failure here means an ABI struct changed : that needs an ABI revision
// (SHARD_ABI_MINOR, and the numbers below updated in the same change), never a quiet edit. 64 bit targets only (x86_64, arm64).

#if INTPTR_MAX == INT64_MAX

static_assert(sizeof(ShardStr) == 16 && offsetof(ShardStr, data) == 0 && offsetof(ShardStr, size) == 8, "ShardStr");

static_assert(sizeof(ShardHostApi) == 64, "ShardHostApi");
static_assert(offsetof(ShardHostApi, structSize) == 0, "ShardHostApi");
static_assert(offsetof(ShardHostApi, abiMajor) == 4 && offsetof(ShardHostApi, abiMinor) == 8, "ShardHostApi");
static_assert(offsetof(ShardHostApi, engineVersion) == 16, "ShardHostApi");
static_assert(offsetof(ShardHostApi, log) == 32 && offsetof(ShardHostApi, alloc) == 40, "ShardHostApi");
static_assert(offsetof(ShardHostApi, free) == 48 && offsetof(ShardHostApi, query_interface) == 56, "ShardHostApi");

static_assert(sizeof(ShardPluginInfo) == 48, "ShardPluginInfo");
static_assert(offsetof(ShardPluginInfo, structSize) == 0, "ShardPluginInfo");
static_assert(offsetof(ShardPluginInfo, abiMajor) == 4 && offsetof(ShardPluginInfo, abiMinor) == 8, "ShardPluginInfo");
static_assert(offsetof(ShardPluginInfo, id) == 16 && offsetof(ShardPluginInfo, user) == 32, "ShardPluginInfo");
static_assert(offsetof(ShardPluginInfo, callbacks) == 40, "ShardPluginInfo");

static_assert(sizeof(ShardPluginCallbacks) == 48, "ShardPluginCallbacks");
static_assert(offsetof(ShardPluginCallbacks, structSize) == 0, "ShardPluginCallbacks");
static_assert(offsetof(ShardPluginCallbacks, on_load) == 8 && offsetof(ShardPluginCallbacks, on_initialize) == 16, "ShardPluginCallbacks");
static_assert(offsetof(ShardPluginCallbacks, on_enable) == 24 && offsetof(ShardPluginCallbacks, on_disable) == 32, "ShardPluginCallbacks");
static_assert(offsetof(ShardPluginCallbacks, on_unload) == 40, "ShardPluginCallbacks");

static_assert(sizeof(ShardPluginApi) == 32, "ShardPluginApi");
static_assert(offsetof(ShardPluginApi, structSize) == 0 && offsetof(ShardPluginApi, id) == 8, "ShardPluginApi");
static_assert(offsetof(ShardPluginApi, version) == 16 && offsetof(ShardPluginApi, directory) == 24, "ShardPluginApi");

static_assert(sizeof(ShardCommandsApi) == 16 && offsetof(ShardCommandsApi, add) == 8, "ShardCommandsApi");

static_assert(sizeof(ShardCVarsApi) == 104, "ShardCVarsApi");
static_assert(offsetof(ShardCVarsApi, structSize) == 0 && offsetof(ShardCVarsApi, create_bool) == 8, "ShardCVarsApi");
static_assert(offsetof(ShardCVarsApi, create_int) == 16 && offsetof(ShardCVarsApi, create_float) == 24, "ShardCVarsApi");
static_assert(offsetof(ShardCVarsApi, create_string) == 32 && offsetof(ShardCVarsApi, get_bool) == 40, "ShardCVarsApi");
static_assert(offsetof(ShardCVarsApi, get_int) == 48 && offsetof(ShardCVarsApi, get_float) == 56, "ShardCVarsApi");
static_assert(offsetof(ShardCVarsApi, get_string) == 64 && offsetof(ShardCVarsApi, set_bool) == 72, "ShardCVarsApi");
static_assert(offsetof(ShardCVarsApi, set_int) == 80 && offsetof(ShardCVarsApi, set_float) == 88, "ShardCVarsApi");
static_assert(offsetof(ShardCVarsApi, set_string) == 96, "ShardCVarsApi");

#endif

static_assert(sizeof(ShardResult) == 4 && sizeof(ShardPluginCtx) == 8 && sizeof(ShardCVar) == 8 && SHARD_CVAR_READONLY == 1u, "handles and results have a fixed size");
static_assert(SHARD_OK == 0 && SHARD_E_INVALID_ARG < 0 && SHARD_E_INTERNAL < SHARD_E_INVALID_ARG, "errors are negative");

// The values plugins compile in : they never change inside a major revision
static_assert(SHARD_E_INVALID_ARG == -1 && SHARD_E_NOT_FOUND == -2 && SHARD_E_ALREADY_EXISTS == -3 && SHARD_E_WRONG_THREAD == -4, "result codes");
static_assert(SHARD_E_STALE_HANDLE == -5 && SHARD_E_UNSUPPORTED == -6 && SHARD_E_OUT_OF_MEMORY == -7 && SHARD_E_INTERNAL == -8, "result codes");
static_assert(SHARD_ABI_MAJOR == 1 && SHARD_ABI_MINOR == 0, "the ABI revision of these headers");

TEST(AbiLayout, HeadersCompileAsCppAndPinTheirLayout)
{
    // The assertions above are the test : this case makes sure the file is part of the run
    EXPECT_EQ(sizeof(ShardStr), 16u);
}
