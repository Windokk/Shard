#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include <shard/shard_services.h>

#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/version.hpp"

// shard.services : what a C plugin provides is a ShardService the host owns (its table and self), registered in the engine's ServiceRegistry
// like a C++ service ; what it asks for is a ServiceRef kept in a table of handles.

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        std::mutex gServicesMutex;

        // The ShardService objects C plugins provide : resolve only reads an object it knows is one
        struct Provided {
            PluginOwner owner = 0;
            std::unique_ptr<ShardService> service;
        };
        std::vector<Provided> gProvided;

        struct RefSlot {
            PluginOwner owner = 0;
            ServiceRef<ShardService> ref;
            bool used = false;
            uint32_t generation = 1;
        };
        std::vector<RefSlot> gRefs;

        RefSlot* FindRef(PluginOwner owner, ShardServiceRef handle)
        {
            const uint64_t index = handle & 0xFFFFFFFFull;
            if (index == 0 || index > gRefs.size()) return nullptr;
            RefSlot& slot = gRefs[size_t(index - 1)];
            if (!slot.used || slot.owner != owner || slot.generation != uint32_t(handle >> 32)) return nullptr;
            return &slot;
        }

        ShardResult SHARD_CALL Provide(ShardPluginCtx handle, ShardStr name, uint32_t major, uint32_t minor, const void* table, void* self)
        {
            try {
                if (!table || !name.data || name.size == 0) return SHARD_E_INVALID_ARG;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                auto service = std::make_unique<ShardService>();
                service->structSize = sizeof(ShardService);
                service->major = major;
                service->minor = minor;
                service->table = table;
                service->self = self;
                ShardService* raw = service.get();
                {
                    std::lock_guard<std::mutex> lock(gServicesMutex);
                    gProvided.push_back(Provided{ PluginOwner(handle), std::move(service) });
                }
                if (!ctx->Services().Provide<ShardService>(ToString(name), major, raw, minor)) {
                    std::lock_guard<std::mutex> lock(gServicesMutex);
                    gProvided.erase(std::remove_if(gProvided.begin(), gProvided.end(), [raw](const Provided& p) { return p.service.get() == raw; }), gProvided.end());
                    return SHARD_E_ALREADY_EXISTS;
                }
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Get(ShardPluginCtx handle, ShardStr name, ShardStr range, ShardServiceRef* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                const std::string wanted = range.size ? ToString(range) : std::string("*");
                if (!VersionRange::Parse(wanted)) return SHARD_E_INVALID_ARG;
                ServiceRef<ShardService> ref = ctx->Services().Get<ShardService>(ToString(name), wanted);
                if (!ref) return SHARD_E_NOT_FOUND;

                std::lock_guard<std::mutex> lock(gServicesMutex);
                size_t index = gRefs.size();
                for (size_t i = 0; i < gRefs.size(); ++i) if (!gRefs[i].used) { index = i; break; }
                if (index == gRefs.size()) gRefs.emplace_back();
                RefSlot& slot = gRefs[index];
                slot.owner = PluginOwner(handle);
                slot.ref = std::move(ref);
                slot.used = true;
                *out = (ShardServiceRef(slot.generation) << 32) | ShardServiceRef(index + 1);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Resolve(ShardPluginCtx handle, ShardServiceRef ref, ShardService* out)
        {
            try {
                if (!out || out->structSize < sizeof(uint32_t)) return SHARD_E_INVALID_ARG;
                std::lock_guard<std::mutex> lock(gServicesMutex);
                RefSlot* slot = FindRef(PluginOwner(handle), ref);
                if (!slot) return SHARD_E_STALE_HANDLE;
                ShardService* service = slot->ref.Get();
                if (!service) return SHARD_E_NOT_FOUND;
                const bool known = std::any_of(gProvided.begin(), gProvided.end(), [service](const Provided& p) { return p.service.get() == service; });
                if (!known) return SHARD_E_UNSUPPORTED;
                ShardService copy = *service;
                copy.structSize = std::min<uint32_t>(out->structSize, sizeof(ShardService));
                std::memcpy(out, &copy, copy.structSize);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Release(ShardPluginCtx handle, ShardServiceRef ref)
        {
            try {
                std::lock_guard<std::mutex> lock(gServicesMutex);
                RefSlot* slot = FindRef(PluginOwner(handle), ref);
                if (!slot) return SHARD_E_STALE_HANDLE;
                slot->ref = ServiceRef<ShardService>();
                slot->used = false;
                slot->owner = 0;
                if (++slot->generation == 0) slot->generation = 1;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    const void* ServicesApi()
    {
        static const ShardServicesApi api = { sizeof(ShardServicesApi), &Provide, &Get, &Resolve, &Release };
        return &api;
    }

    void ReleaseServices(PluginOwner owner)
    {
        std::lock_guard<std::mutex> lock(gServicesMutex);
        for (RefSlot& slot : gRefs) {
            if (!slot.used || slot.owner != owner) continue;
            slot.ref = ServiceRef<ShardService>();
            slot.used = false;
            slot.owner = 0;
            if (++slot.generation == 0) slot.generation = 1;
        }
        // Its services were revoked by the context (their refs are empty now) : the objects can go
        gProvided.erase(std::remove_if(gProvided.begin(), gProvided.end(), [owner](const Provided& p) { return p.owner == owner; }), gProvided.end());
    }
}
