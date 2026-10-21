#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include <shard/shard_abi.h>
#include <shard/shard_components.h>
#include <shard/shard_ecs.h>

#include "engine/plugins/context.hpp"

// Shared by the files of the host side of the C ABI (src/engine/plugins/abi) : not an API of the engine. Each interface lives in a file of its
// own (c_components.cpp, c_systems.cpp...) that builds its table ; c_plugin.cpp hands the tables out (query_interface) and forgets the handles
// of a plugin when its context releases everything.

namespace Shard::Engine::Core::Ecs { struct SystemContext; }

namespace Shard::Engine::Plugins::CAbi {

    // ---- Plugins ----

    /// The context a ShardPluginCtx designates, null once the plugin's context released everything (the handle outlived it)
    PluginContext* FindContext(ShardPluginCtx handle);
    /// Whether that plugin may use the "shard.experimental.*" interfaces (see MayUseExperimental)
    bool ExperimentalAllowed(ShardPluginCtx handle);

    // ---- Strings ----

    inline std::string ToString(ShardStr s) { return s.data && s.size ? std::string(s.data, size_t(s.size)) : std::string(); }
    inline ShardStr ToStr(const std::string& s) { return ShardStr{ s.data(), uint64_t(s.size()) }; }

    /// The "text out" of the ABI : at most `capacity` bytes of `text` into `buffer` (no terminator), its full length in *size
    ShardResult CopyOut(const std::string& text, char* buffer, uint64_t capacity, uint64_t* size);

    // ---- Crash attribution ----

    /// @brief Names the plugin as the one whose code runs (the manager's activePlugin, what a crash report names) while a callback of it runs,
    /// and puts back whoever was named before. Every call from the host into a plugin goes through one.
    class ActiveScope {
    public:
        ActiveScope(std::atomic<uint32_t>* active, PluginOwner owner) : m_Active(active)
        {
            if (m_Active) m_Previous = m_Active->exchange(owner, std::memory_order_relaxed);
        }
        ~ActiveScope() { if (m_Active) m_Active->store(m_Previous, std::memory_order_relaxed); }
        ActiveScope(const ActiveScope&) = delete;
        ActiveScope& operator=(const ActiveScope&) = delete;
    private:
        std::atomic<uint32_t>* m_Active;
        uint32_t m_Previous = 0;
    };

    /// The counter of the context's host : captured when a callback is registered (it lives as long as the manager)
    inline std::atomic<uint32_t>* ActiveCounter(PluginContext& ctx) { return ctx.Bindings().activePlugin; }

    // ---- Threads ----

    /// The thread the plugins are loaded on : noted when the first plugin is bound
    void NoteMainThread();
    /// True on that thread, outside of a system : where the functions documented "thread : main" may be called
    bool OnMainThread();

    // ---- System runs (c_systems.cpp) ----

    struct CSystem;

    /// An event a system emitted : delivered at the end of the frame, on the main thread (c_events.cpp)
    struct QueuedEvent {
        bool toWorld = false;
        int32_t world = 0;
        std::string name;
        std::vector<unsigned char> payload;
    };

    struct SystemRun {
        ShardEcs handle = 0;
        Core::Ecs::SystemContext* context = nullptr;
        const CSystem* system = nullptr;            ///< null : may touch any type (shard.experimental.world_ecs)
        bool mainThread = false;                    ///< opened from the main thread (world_ecs) : not a system, the main thread functions still work
        std::vector<QueuedEvent> events;
    };

    /// The run of a C system in progress on this thread, null outside of one
    SystemRun* CurrentRun();

    // ---- Events (c_events.cpp) ----

    /// Keeps what a system emitted until the end of the frame, under the system's name (the order of delivery)
    void QueueEvents(const std::string& systemName, std::vector<QueuedEvent>& events);

    // ---- State (c_state.cpp) ----

    /// Makes `writer` (on_prepare_reload) or `reader` (on_restore) reachable through the handle returned, until EndState
    ShardState BeginState(StateWriter* writer, const StateReader* reader);
    void EndState();

    // ---- Fields (c_components.cpp) ----

    /// The size and alignment of a SHARD_FIELD_* type ; false for a type the ABI does not have
    bool FieldLayoutOf(uint32_t type, uint32_t& size, uint32_t& align);
    /// Letters, digits and '_', not a reserved name
    bool IsFieldName(const std::string& name);

    // ---- The interfaces ----

    const void* ComponentsApi();
    const void* EcsApi();
    const void* SystemsApi();
    const void* EventsApi();
    const void* HooksApi();
    const void* ServicesApi();
    const void* ExtensionsApi();
    const void* ContentApi();
    const void* StateApi();

    const void* WorldEcsApi();

    /// The plugin's context releases everything : what the interfaces keep about it goes. `ctx` is still there (blobs give its memory back).
    void ReleaseComponents(PluginOwner owner);
    void ReleaseServices(PluginOwner owner);
    void ReleaseBlobs(PluginOwner owner, PluginContext& ctx);
    void CloseWorldEcs(PluginOwner owner);

    // ---- Blobs (c_components.cpp), for the actor components that own theirs ----

    /// A copy of a blob of `owner` (0 if it has none or the plugin is gone)
    ShardBlob CopyBlob(PluginOwner owner, ShardBlob blob);
    void FreeBlob(PluginOwner owner, ShardBlob blob);
    /// The bytes of a blob (false if it is not one of `owner`'s)
    bool BlobBytes(PluginOwner owner, ShardBlob blob, std::string& out);
    /// A new blob of `owner` holding `bytes` (0 if the plugin is gone or over its budget)
    ShardBlob BlobFromBytes(PluginOwner owner, const std::string& bytes);
}
