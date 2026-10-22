#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include <memory>

#include <shard/shard_abi.h>
#include <shard/shard_actors.h>
#include <shard/shard_components.h>
#include <shard/shard_ecs.h>

#include "engine/plugins/context.hpp"

// Shared by the files of the host side of the C ABI (src/engine/plugins/abi) : not an API of the engine. Each interface lives in a file of its
// own (c_components.cpp, c_systems.cpp...) that builds its table ; c_plugin.cpp hands the tables out (query_interface) and forgets the handles
// of a plugin when its context releases everything.

namespace Shard::Engine::Core::Ecs { struct SystemContext; }
namespace Shard::Engine::Objects { class Actor; }

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

    /// While one lives, this thread runs a callback of a script: the scripts of a world are ticked one after the other, so the functions
    /// documented "thread : script" (actors, input) are safe there, on whatever thread the engine's script systems landed on.
    class ScriptScope {
    public:
        ScriptScope();
        ~ScriptScope();
        ScriptScope(const ScriptScope&) = delete;
        ScriptScope& operator=(const ScriptScope&) = delete;
    };
    /// Inside a script callback, or on the main thread outside the systems: where the functions documented "thread : script" may be called
    bool OnScriptThread();

    // ---- Actors, scripts, input (c_actors.cpp, c_scripts.cpp, c_input.cpp) ----

    /// The handle of an actor (the same one each time while the actor lives), and the actor behind a handle (null: stale or never valid)
    ShardActor ActorHandle(const std::shared_ptr<Objects::Actor>& actor);
    std::shared_ptr<Objects::Actor> ActorOf(ShardActor handle);

    /// The path of an actor in its world, the names from the root joined by '/' ("Level/Enemies/Boss"), "" for a handle that is not an actor any more
    std::string ActorPath(ShardActor handle);
    /// The actor of the world `anchor` is in that has that path (the first of that name at each level, in the order of the ids), 0 if there is none
    ShardActor ActorAtPath(const std::shared_ptr<Objects::Actor>& anchor, const std::string& path);

    /// The plugin's context releases everything: its script types stop being called (the objects that are still in worlds are left alone)
    void ReleaseScripts(PluginOwner owner);

    // ---- System runs (c_systems.cpp) ----

    struct CSystem;

    /// An event a system emitted : delivered at the end of the frame, on the main thread (c_events.cpp)
    struct QueuedEvent {
        bool toWorld = false;
        int32_t world = 0;
        std::string name;
        std::vector<unsigned char> payload;
    };

    /// A pose a system set on the actor behind an entity (shard.ecs set_local_transform) : applied to the actor's Transform at the end of the phase
    struct QueuedPose {
        uint64_t entity = 0;
        ShardLocalTransform pose{};
    };

    struct SystemRun {
        ShardEcs handle = 0;
        Core::Ecs::SystemContext* context = nullptr;
        const CSystem* system = nullptr;            ///< null : may touch any type (shard.experimental.world_ecs)
        bool mainThread = false;                    ///< opened from the main thread (world_ecs) : not a system, the main thread functions still work
        std::vector<QueuedEvent> events;
        std::vector<QueuedPose> poses;
    };

    /// The run of a C system in progress on this thread, null outside of one
    SystemRun* CurrentRun();

    /// Keeps the poses a system set until the end of the phase, under the system's name (the order they are applied in)
    void QueuePoses(const std::string& systemName, std::vector<QueuedPose>& poses);
    /// Gives each pose to the Transform of its actor (main thread, registry not iterated). Poses that find no actor are dropped.
    void ApplyPoses(Core::IEngineContext* engine, const std::vector<QueuedPose>& poses);

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
    const void* ActorsApi();
    const void* ScriptsApi();
    const void* InputApi();
    const void* PhysicsApi();
    const void* AudioApi();
    const void* WorldsApi();
    const void* EditorApi();
    /// True once the editor gave its widgets (SetEditorUiBackend) : shard.editor exists only then
    bool EditorUiAvailable();

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
