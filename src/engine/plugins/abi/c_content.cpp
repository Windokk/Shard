#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>

#include <nlohmann/json.hpp>

#include <shard/shard_content.h>
#include <shard/shard_state.h>

#include "engine/plugins/abi/c_internal.hpp"

// shard.content (the ContentMounts of the manager) and shard.state (the StateWriter / StateReader of a hot reload)

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        // ---- shard.content ----

        ShardResult SHARD_CALL UriOf(ShardPluginCtx handle, ShardStr path, char* buffer, uint64_t capacity, uint64_t* size)
        {
            try {
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                return CopyOut(ctx->Assets().Uri(ToString(path)), buffer, capacity, size);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Resolve(ShardPluginCtx handle, ShardStr uri, char* buffer, uint64_t capacity, uint64_t* size)
        {
            try {
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                const auto file = ctx->Content().Resolve(ToString(uri));
                if (!file) return SHARD_E_NOT_FOUND;
                return CopyOut(*file, buffer, capacity, size);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ResolveLogical(ShardPluginCtx handle, ShardStr logicalPath, char* buffer, uint64_t capacity, uint64_t* size)
        {
            try {
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                const auto found = ctx->Content().ResolveLogical(ToString(logicalPath));
                if (!found) return SHARD_E_NOT_FOUND;
                return CopyOut(found->uri, buffer, capacity, size);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Read(ShardPluginCtx handle, ShardStr uri, void** data, uint64_t* size)
        {
            try {
                if (!data || !size) return SHARD_E_INVALID_ARG;
                *data = nullptr;
                *size = 0;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                const auto file = ctx->Content().Resolve(ToString(uri));
                if (!file) return SHARD_E_NOT_FOUND;
                std::ifstream in(*file, std::ios::binary);
                if (!in) return SHARD_E_NOT_FOUND;
                const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                // At least one byte, so that an empty file is a valid pointer the plugin frees like any other
                void* block = ctx->Memory().Allocate(bytes.empty() ? 1 : bytes.size(), 16);
                if (!block) return SHARD_E_OUT_OF_MEMORY;
                if (!bytes.empty()) std::memcpy(block, bytes.data(), bytes.size());
                *data = block;
                *size = bytes.size();
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL List(ShardPluginCtx handle, ShardStr pluginId, ShardStr subdir, ShardListFn fn, void* user)
        {
            try {
                if (!fn) return SHARD_E_INVALID_ARG;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                const std::string id = pluginId.size ? ToString(pluginId) : ctx->Id();
                if (!ctx->Content().Find(id)) return SHARD_E_NOT_FOUND;
                for (const std::string& path : ctx->Content().List(id, ToString(subdir))) {
                    const ShardResult result = fn(user, ToStr(path));
                    if (result != SHARD_OK) return result;
                }
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // ---- shard.state ----

        // One state at a time : the manager reloads one plugin at a time, on the main thread
        std::mutex gStateMutex;
        ShardState gState = 0;
        uint64_t gNextState = 0;
        StateWriter* gWriter = nullptr;
        const StateReader* gReader = nullptr;

        ShardResult SHARD_CALL StateSet(ShardState state, ShardStr key, ShardStr json)
        {
            try {
                std::lock_guard<std::mutex> lock(gStateMutex);
                if (state == 0 || state != gState) return SHARD_E_STALE_HANDLE;
                if (!gWriter) return SHARD_E_UNSUPPORTED;
                const std::string text = ToString(json);
                nlohmann::json value = nlohmann::json::parse(text, nullptr, false);
                if (value.is_discarded()) return SHARD_E_INVALID_ARG;
                gWriter->SetJson(ToString(key), std::move(value));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL StateGet(ShardState state, ShardStr key, char* buffer, uint64_t capacity, uint64_t* size)
        {
            try {
                std::lock_guard<std::mutex> lock(gStateMutex);
                if (state == 0 || state != gState) return SHARD_E_STALE_HANDLE;
                if (!gReader) return SHARD_E_UNSUPPORTED;
                const std::string k = ToString(key);
                if (!gReader->Has(k)) return SHARD_E_NOT_FOUND;
                return CopyOut(gReader->GetJson(k).dump(), buffer, capacity, size);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        int32_t SHARD_CALL StateHas(ShardState state, ShardStr key)
        {
            try {
                std::lock_guard<std::mutex> lock(gStateMutex);
                if (state == 0 || state != gState) return SHARD_FALSE;
                if (gReader) return gReader->Has(ToString(key)) ? SHARD_TRUE : SHARD_FALSE;
                if (gWriter) return gWriter->Data().contains(ToString(key)) ? SHARD_TRUE : SHARD_FALSE;
                return SHARD_FALSE;
            } catch (...) { return SHARD_FALSE; }
        }
    }

    ShardState BeginState(StateWriter* writer, const StateReader* reader)
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        gWriter = writer;
        gReader = reader;
        gState = ++gNextState;
        return gState;
    }

    void EndState()
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        gWriter = nullptr;
        gReader = nullptr;
        gState = 0;
    }

    const void* ContentApi()
    {
        static const ShardContentApi api = { sizeof(ShardContentApi), &UriOf, &Resolve, &ResolveLogical, &Read, &List };
        return &api;
    }

    const void* StateApi()
    {
        static const ShardStateApi api = { sizeof(ShardStateApi), &StateSet, &StateGet, &StateHas };
        return &api;
    }
}
