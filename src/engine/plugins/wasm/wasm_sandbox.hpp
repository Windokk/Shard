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

// The state the host keeps for one sandboxed plugin : its instance, the budgets, the memory the host manages inside the module's linear memory,
// the copies it hands the module of what lives in host memory, and what the module registered (docs/design-notes/wasm-sandbox.fr.md).

namespace Shard::Engine::Plugins::Wasm {

    /// The callbacks of the wasm32 ShardPluginCallbacks, in the order of the struct (4 bytes each after structSize) : indices in the module's table
    enum Slot : uint32_t {
        OnLoad, OnInitialize, OnEnable, OnDisable, OnUnload, OnPrepareReload, OnRestore, OnFrameBegin, OnFrameEnd,
        OnContentMounted, OnWorldLoaded, OnWorldUnloaded, OnPlayStart, OnPlayStop, SlotCount
    };

    /// Which budget a call into the module is held to : the frame's (shared by everything that runs in it) or one of its own
    enum class Budget { Call, Frame };

    /// @brief Memory the host manages inside a module's linear memory, for what it has to put there : the arguments of the callbacks it makes,
    /// the copies of host data, the objects of scripts. The module never sees these pages in its own allocator : they come from memory.grow
    /// (so they count against budgets.memoryMB), and the host keeps all its bookkeeping outside the module.
    class SandboxHeap {
    public:
        void Attach(Instance* instance) { m_In = instance; }

        /// Lasting memory (zeroed), 16-byte aligned at most. 0 when the module's memory cannot grow any more.
        uint32_t Alloc(uint32_t size, uint32_t align = 16);
        /// false if `at` is not a live block of this heap
        bool Free(uint32_t at);
        /// Memory until ResetTemp : the arguments of one callback, a copy of a column. Not zeroed. 0 on failure.
        uint32_t Temp(uint64_t size, uint32_t align = 16);
        void ResetTemp();
        /// Temporary memory is a stack : what was taken after a mark is given back by Release(mark) (a loop that copies a chunk at a time)
        struct Mark { size_t region = 0; uint32_t used = 0; };
        Mark TempMark() const { return Mark{ m_TempAt, m_Temp.empty() ? 0u : m_Temp[m_TempAt].used }; }
        void TempRelease(const Mark& mark);
        /// A zero terminated copy of `text` that lives as long as the heap (the same text gives the same address)
        uint32_t Intern(const std::string& text);

        uint64_t ReservedBytes() const { return m_Reserved; }

    private:
        struct Region { uint32_t base = 0, size = 0, used = 0; };
        bool NewRegion(std::vector<Region>& regions, uint64_t minBytes, uint32_t minPages);

        Instance* m_In = nullptr;
        std::vector<Region> m_Persistent;
        std::vector<Region> m_Temp;
        size_t m_TempAt = 0;                                           ///< the region temporary memory is taken from
        std::unordered_map<uint32_t, uint32_t> m_Live;                 ///< address -> size class
        std::map<uint32_t, std::vector<uint32_t>> m_Free;              ///< size class -> addresses
        std::unordered_map<std::string, uint32_t> m_Interned;
        uint64_t m_Reserved = 0;
    };

    /// What the module registered with the host (a command, a system, an event handler, a script type) : owned by the sandbox, dropped when the
    /// plugin is loaded again (its registrations went with the previous context)
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

        ShardPluginCallbacks native{};                      ///< what CAbiPlugin calls : trampolines into the module

        // The module's entry point filled these
        uint32_t user = 0;                                  ///< the plugin's `user`, an address in its memory : handed back as it is
        uint32_t callbacks[SlotCount] = {};                 ///< table indices, 0 : none

        // Budgets
        uint64_t fuel = 0;                                  ///< per frame, and per call outside a frame
        uint64_t frameLeft = 0;
        uint64_t fuelUsed = 0;                              ///< what the last outermost call burnt (the calibration of kWasmFuelPerMs)
        uint64_t fuelUsedTotal = 0;                         ///< ... and all of them together
        std::atomic<bool> frameOpen{ false };
        bool dead = false;                                  ///< after a trap : the instance's state can no longer be trusted

        // One call at a time ; the same thread may call back (a host function that calls into the module)
        std::recursive_mutex mutex;
        int depth = 0;
        uint64_t fuelAtStart = 0;
        bool callInFrame = false;                           ///< the outermost call in progress draws on the frame's budget

        std::vector<std::unique_ptr<Registration>> registrations;

        /// The system being run, for ecs.get : which components the module may write back (null outside a system)
        struct Run {
            ShardEcs ecs = 0;
            bool exclusive = false;
            const std::vector<ShardComponentType>* writes = nullptr;
        };
        Run* run = nullptr;

        /// The widgets and the gizmo painter of the callback of the module that is drawing (shard.editor) : its ui.* and gizmo.* imports act on them,
        /// and do nothing when the host is not drawing
        const ShardUiApi* ui = nullptr;
        const ShardGizmoApi* gizmo = nullptr;
        /// The addresses of the tables the module built from the imports (the globals it exports), handed to its callbacks
        uint32_t uiTableAt = 0, gizmoTableAt = 0;

        /// The columns of the chunk being run (ecs.each_chunk copies a piece of them into the module) : ecs.get for an entity of the piece gives the
        /// address inside that copy, so the module sees one block whichever way it reached it
        struct Window {
            const uint8_t* host = nullptr;
            uint64_t bytes = 0;
            uint32_t wasm = 0;
            uint32_t blockSize = 0;
        };
        std::vector<Window> windows;

        bool Allowed(Permission p) const { return p == Permission::None || permissions[uint8_t(p)]; }

        // ---- Calling into the module ----

        /// Runs the function in slot `index` of the module's table. A trap (or an exhausted budget) stops the plugin : the journal says why, and
        /// false is returned. The copies the module was given are written back and the temporary memory released when the outermost call ends.
        bool CallTable(uint32_t index, const FuncType& type, const std::vector<uint64_t>& args, Budget budget, std::vector<uint64_t>& results);
        /// ... and for an exported function (`shard_plugin_entry`, `_initialize`). `message` : why it failed, when it did.
        bool CallExport(const std::string& name, const std::vector<uint64_t>& args, Budget budget, std::vector<uint64_t>& results, std::string* message = nullptr);
        /// CallTable for a function that returns a ShardResult : SHARD_E_INTERNAL when it trapped
        ShardResult CallResult(uint32_t index, const FuncType& type, const std::vector<uint64_t>& args, Budget budget);

        void Log(uint32_t level, const std::string& message);
        /// Stops the plugin : nothing runs in the module any more
        void Kill(const std::string& message);

        // ---- What the module is given of host memory ----

        /// A copy of `size` bytes of host memory in the module (valid until the outermost call returns), written back to `host` when it ends if
        /// `writeBack`. The same host block gives the same copy for the whole call. 0 when the module has no room left.
        uint32_t MirrorTemp(void* host, uint32_t size, bool writeBack);
        /// A lasting copy of a blob's bytes (the module may keep the pointer until it resizes or frees the blob), written back when a call ends
        uint32_t MirrorBlob(ShardBlob blob, void* host, uint64_t size);
        /// Writes the module's copy of the blob back and forgets it (before the host resizes or frees the blob)
        void DropBlobMirror(ShardBlob blob);
        /// Something to do when the outermost call ends, before the temporary memory goes (a script's fields back to its object)
        void OnCallEnd(std::function<void()> fn) { m_AfterCall.push_back(std::move(fn)); }

        /// Gives the module a lasting copy of `text` and writes the wasm32 ShardStr at `sret` : the way the string a function returns by value reaches it
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

    // The types of the callbacks the host makes (their wasm32 signature, as Clang gives them)
    inline const FuncType kTypeLifecycle{ { ValType::I32, ValType::I64 }, { ValType::I32 } };                       // on_load...
    inline const FuncType kTypeFrame{ { ValType::I32, ValType::I64, ValType::I64 }, { ValType::I32 } };             // on_frame_begin
    inline const FuncType kTypeState{ { ValType::I32, ValType::I64, ValType::I64 }, { ValType::I32 } };             // on_prepare_reload (state is a u64)
    inline const FuncType kTypeWorld{ { ValType::I32, ValType::I64, ValType::I32 }, { ValType::I32 } };             // on_world_loaded...
}
