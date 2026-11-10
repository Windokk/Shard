#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <memory>

#include <shard/shard_abi.h>
#include <shard/shard_actors.h>
#include <shard/shard_assets.h>
#include <shard/shard_components.h>
#include <shard/shard_ecs.h>

#include "engine/plugins/context.hpp"

namespace Shard::Engine::Core::Ecs { struct SystemContext; }
namespace Shard::Engine::Objects { class Actor; }
namespace Shard::Engine::Events {
    struct ContactAddedEvent;
    struct ContactPersistedEvent;
    struct ContactRemovedEvent;
}

namespace Shard::Engine::Plugins::CAbi {

    PluginContext* FindContext(ShardPluginCtx handle);
    bool ExperimentalAllowed(ShardPluginCtx handle);

    inline std::string ToString(ShardStr s) { return s.data && s.size ? std::string(s.data, size_t(s.size)) : std::string(); }
    inline ShardStr ToStr(const std::string& s) { return ShardStr{ s.data(), uint64_t(s.size()) }; }

    ShardResult CopyOut(const std::string& text, char* buffer, uint64_t capacity, uint64_t* size);

    // Names the plugin as active (crash reports use it) while its callback runs, restores the previous one on exit.
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

    inline std::atomic<uint32_t>* ActiveCounter(PluginContext& ctx) { return ctx.Bindings().activePlugin; }

    void NoteMainThread();
    bool OnMainThread();
    bool IsMainThreadId(std::thread::id id);

    // While alive, this thread is running a script callback. Scripts of a world tick one after another, so "thread: script" functions are safe on any thread the script systems landed on.
    class ScriptScope {
    public:
        ScriptScope();
        ~ScriptScope();
        ScriptScope(const ScriptScope&) = delete;
        ScriptScope& operator=(const ScriptScope&) = delete;
    };
    bool OnScriptThread();

    ShardActor ActorHandle(const std::shared_ptr<Objects::Actor>& actor);
    std::shared_ptr<Objects::Actor> ActorOf(ShardActor handle);

    std::string ActorPath(ShardActor handle);
    ShardActor ActorAtPath(const std::shared_ptr<Objects::Actor>& anchor, const std::string& path);

    bool ActorBounds(const std::shared_ptr<Objects::Actor>& actor, float outMin[3], float outMax[3]);

    bool ResolveProjectFile(PluginContext& ctx, const std::string& path, std::string& file);

    ShardResult AssetMeshInfo(int32_t id, ShardMeshInfo* out);
    ShardResult AssetMeshRead(int32_t id, ShardMeshRead* io);
    ShardResult AssetImageInfo(int32_t id, uint32_t* width, uint32_t* height);
    ShardResult AssetImageRead(int32_t id, uint8_t* out, uint64_t capacity, uint64_t* size);

    void ReleaseScripts(PluginOwner owner);

    // Normal is Jolt's, first body to second. Decoded in the engine root (c_abi_physics.cpp) because this layer cannot include physics.
    struct ContactInfo {
        std::shared_ptr<Objects::Actor> other;
        bool hasManifold = false;
        float normal[3] = {};
        bool hasPoint = false;
        float point[3] = {};
        float penetration = 0.0f;
    };
    ContactInfo DecodeContact(const Events::ContactAddedEvent& event);
    ContactInfo DecodeContact(const Events::ContactPersistedEvent& event);
    ContactInfo DecodeContact(const Events::ContactRemovedEvent& event);

    struct CSystem;

    struct QueuedEvent {
        bool toWorld = false;
        int32_t world = 0;
        std::string name;
        std::vector<unsigned char> payload;
    };

    struct QueuedPose {
        uint64_t entity = 0;
        ShardLocalTransform pose{};
    };

    struct SystemRun {
        ShardEcs handle = 0;
        Core::Ecs::SystemContext* context = nullptr;
        const CSystem* system = nullptr;
        bool mainThread = false;
        std::vector<QueuedEvent> events;
        std::vector<QueuedPose> poses;
    };

    SystemRun* CurrentRun();

    void QueuePoses(const std::string& systemName, std::vector<QueuedPose>& poses);
    void ApplyPoses(Core::IEngineContext* engine, const std::vector<QueuedPose>& poses);

    void QueueEvents(const std::string& systemName, std::vector<QueuedEvent>& events);

    ShardState BeginState(StateWriter* writer, const StateReader* reader);
    void EndState();

    bool FieldLayoutOf(uint32_t type, uint32_t& size, uint32_t& align);
    bool IsFieldName(const std::string& name);

    void RunMainPosts();
    void NetPollAll();
    void DebugEndFrame();
    void DebugClearAll();
    void InputBeginFrame(uint64_t frame);
    void ReleaseInput(PluginOwner owner);
    // Main thread only, while the physics world is not stepping
    void PhysicsBeginFrame();

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
    const void* JobsApi();
    const void* TimeApi();
    const void* TimersApi();
    const void* AssetsApi();
    const void* ReflectApi();
    const void* NetApi();
    const void* RenderApi();
    const void* DebugApi();
    const void* EditorApi();
    bool EditorUiAvailable();

    void ReleaseComponents(PluginOwner owner);
    void ReleaseServices(PluginOwner owner);
    void ReleaseBlobs(PluginOwner owner, PluginContext& ctx);
    void CloseWorldEcs(PluginOwner owner);

    ShardBlob CopyBlob(PluginOwner owner, ShardBlob blob);
    void FreeBlob(PluginOwner owner, ShardBlob blob);
    bool BlobBytes(PluginOwner owner, ShardBlob blob, std::string& out);
    ShardBlob BlobFromBytes(PluginOwner owner, const std::string& bytes);
}
