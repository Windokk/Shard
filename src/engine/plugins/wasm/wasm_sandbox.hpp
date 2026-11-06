#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <shard/shard_host.h>

#include "engine/plugins/context.hpp"
#include "engine/plugins/wasm/wasm_layout_gen.hpp"
#include "engine/plugins/wasm/wasm_runtime.hpp"

namespace Shard::Engine::Plugins::Wasm {

    enum Slot : uint32_t {
        OnLoad, OnInitialize, OnEnable, OnDisable, OnUnload, OnPrepareReload, OnRestore, OnFrameBegin, OnFrameEnd,
        OnContentMounted, OnWorldLoaded, OnWorldUnloaded, OnPlayStart, OnPlayStop, SlotCount
    };

    enum class Budget { Call, Frame };

    // Host-managed pages inside the module's linear memory, taken via memory.grow so they count against budgets.memoryMB. Bookkeeping stays outside the module.
    class SandboxHeap {
    public:
        void Attach(Instance* instance) { m_In = instance; }

        uint32_t Alloc(uint32_t size, uint32_t align = 16);
        bool Free(uint32_t at);
        uint32_t Temp(uint64_t size, uint32_t align = 16);
        void ResetTemp();
        struct Mark { size_t region = 0; uint32_t used = 0; };
        Mark TempMark() const { return Mark{ m_TempAt, m_Temp.empty() ? 0u : m_Temp[m_TempAt].used }; }
        void TempRelease(const Mark& mark);
        uint32_t Intern(const std::string& text);

        uint64_t ReservedBytes() const { return m_Reserved; }

    private:
        struct Region { uint32_t base = 0, size = 0, used = 0; };
        bool NewRegion(std::vector<Region>& regions, uint64_t minBytes, uint32_t minPages);

        Instance* m_In = nullptr;
        std::vector<Region> m_Persistent;
        std::vector<Region> m_Temp;
        size_t m_TempAt = 0;
        std::unordered_map<uint32_t, uint32_t> m_Live;
        std::map<uint32_t, std::vector<uint32_t>> m_Free;
        std::unordered_map<std::string, uint32_t> m_Interned;
        uint64_t m_Reserved = 0;
    };

    struct Registration {
        virtual ~Registration() = default;
    };

    struct Sandbox {
        PluginOwner owner = 0;
        ShardPluginCtx ctx = 0;
        std::string pluginId;
        std::unique_ptr<Instance> instance;
        SandboxHeap heap;
        HostApis apis;
        bool permissions[kPermissionCount] = {};

        ShardPluginCallbacks native{};

        uint32_t user = 0;
        uint32_t callbacks[SlotCount] = {};

        uint64_t fuel = 0;
        uint64_t frameLeft = 0;
        uint64_t fuelUsed = 0;
        uint64_t fuelUsedTotal = 0;
        std::atomic<bool> frameOpen{ false };
        bool dead = false;

        std::recursive_mutex mutex;
        int depth = 0;
        uint64_t fuelAtStart = 0;
        bool callInFrame = false;

        std::vector<std::unique_ptr<Registration>> registrations;

        struct Run {
            ShardEcs ecs = 0;
            bool exclusive = false;
            const std::vector<ShardComponentType>* writes = nullptr;
        };
        Run* run = nullptr;

        const ShardUiApi* ui = nullptr;
        const ShardGizmoApi* gizmo = nullptr;
        uint32_t uiTableAt = 0, gizmoTableAt = 0;

        struct Window {
            const uint8_t* host = nullptr;
            uint64_t bytes = 0;
            uint32_t wasm = 0;
            uint32_t blockSize = 0;
        };
        std::vector<Window> windows;

        bool Allowed(Permission p) const { return p == Permission::None || permissions[uint8_t(p)]; }

        bool CallTable(uint32_t index, const FuncType& type, const std::vector<uint64_t>& args, Budget budget, std::vector<uint64_t>& results);
        bool CallExport(const std::string& name, const std::vector<uint64_t>& args, Budget budget, std::vector<uint64_t>& results, std::string* message = nullptr);
        ShardResult CallResult(uint32_t index, const FuncType& type, const std::vector<uint64_t>& args, Budget budget);

        void Log(uint32_t level, const std::string& message);
        void Kill(const std::string& message);

        uint32_t MirrorTemp(void* host, uint32_t size, bool writeBack);
        uint32_t MirrorBlob(ShardBlob blob, void* host, uint64_t size);
        void DropBlobMirror(ShardBlob blob);
        void OnCallEnd(std::function<void()> fn) { m_AfterCall.push_back(std::move(fn)); }

        bool ReturnString(Instance& in, uint64_t sret, ShardStr text);

        template <typename T, typename... Args>
        T* Register(Args&&... args)
        {
            auto owned = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = owned.get();
            registrations.push_back(std::move(owned));
            return raw;
        }

    private:
        struct TempMirror { void* host; uint32_t at; uint32_t size; bool writeBack; };
        struct BlobMirror { uint32_t at; void* host; uint64_t size; };

        void EndOutermost(bool ok);
        bool Enter(Budget budget, const std::function<Core::Result<std::vector<uint64_t>>()>& invoke, std::vector<uint64_t>& results, std::string* message);

        std::vector<TempMirror> m_Temp;
        std::unordered_map<void*, size_t> m_TempIndex;
        std::unordered_map<ShardBlob, BlobMirror> m_Blobs;
        std::vector<std::function<void()>> m_AfterCall;
    };

    inline const FuncType kTypeLifecycle{ { ValType::I32, ValType::I64 }, { ValType::I32 } };
    inline const FuncType kTypeFrame{ { ValType::I32, ValType::I64, ValType::I64 }, { ValType::I32 } };
    inline const FuncType kTypeState{ { ValType::I32, ValType::I64, ValType::I64 }, { ValType::I32 } };
    inline const FuncType kTypeWorld{ { ValType::I32, ValType::I64, ValType::I32 }, { ValType::I32 } };
}
