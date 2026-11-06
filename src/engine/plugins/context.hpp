#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

#include "engine/core/config/cvar.hpp"
#include "engine/core/ecs/scheduler.hpp"
#include "engine/core/math/random.hpp"
#include "engine/plugins/content.hpp"
#include "engine/plugins/extensions.hpp"
#include "engine/plugins/hooks.hpp"
#include "engine/plugins/manifest.hpp"
#include "engine/plugins/plugin.hpp"
#include "engine/plugins/services.hpp"
#include "engine/world/components/registry/component_registry.hpp"
#include "engine/world/event_system.hpp"
#include "engine/world/world_extension.hpp"

namespace Shard::Engine::Core { class IEngineContext; }
namespace Shard::Engine::Debugging { class Logger; }

namespace Shard::Engine::Plugins {

    namespace Permissions {
        inline constexpr const char* kFsReadContent = "fs.read:content";
        inline constexpr const char* kFsReadProject = "fs.read:project";
        inline constexpr const char* kFsWriteSave = "fs.write:save";
        inline constexpr const char* kNetwork = "net";
        inline constexpr const char* kProcess = "process";
        inline constexpr const char* kRenderFeature = "render.feature";
        inline constexpr const char* kEngineInternal = "engine.internal";
    }

    struct HostBindings {
        Core::IEngineContext* engine = nullptr;
        Core::Ecs::Scheduler* scheduler = nullptr;
        Events::EventDispatcher* events = nullptr;
        Objects::Components::ComponentRegistry* components = nullptr;
        Core::CVarRegistry* cvars = nullptr;
        ServiceRegistry* services = nullptr;
        HookRegistry* hooks = nullptr;
        ExtensionRegistry* extensions = nullptr;
        ContentMounts* content = nullptr;
        Debugging::Logger* logger = nullptr;

        std::function<void*(size_t size, size_t align)> allocate;
        std::function<void(void* pointer, size_t size, size_t align)> deallocate;

        // Process-wide static registries (world_extension.cpp) are copied into each plugin binary, so plugins go through host functions. Set to the owner id around system calls for crash reports.
        std::atomic<uint32_t>* activePlugin = nullptr;
        std::function<void(PluginOwner, const std::string&)> reportError;

        std::function<bool(std::type_index, Worlds::WorldExtensionFactory)> registerWorldExtension;
        std::function<bool(std::type_index)> unregisterWorldExtension;
        std::function<uint32_t(Worlds::WorldSettingsAssetRefs)> registerSettingsRefs;
        std::function<bool(uint32_t)> unregisterSettingsRefs;
    };

    class PluginContext;

    // Memory taken from the host allocator and counted against budgets.memoryMB. Anything allocated another way is not counted. What is left at unload is reported as a leak.
    class PluginMemory {
    public:
        PluginMemory(const HostBindings& host, uint64_t limitBytes, bool enforce) : m_Allocate(host.allocate), m_Deallocate(host.deallocate), m_Limit(limitBytes), m_Enforce(enforce) {}

        void* Allocate(size_t size, size_t align = 16);
        void Free(void* pointer);

        template <typename T, typename... Args>
        T* New(Args&&... args)
        {
            void* p = Allocate(sizeof(T), alignof(T));
            return p ? new (p) T(std::forward<Args>(args)...) : nullptr;
        }
        template <typename T>
        void Delete(T* object)
        {
            if (!object) return;
            object->~T();
            Free(object);
        }

        uint64_t Used() const { return m_Used.load(std::memory_order_relaxed); }
        uint64_t Peak() const { return m_Peak.load(std::memory_order_relaxed); }
        uint64_t Limit() const { return m_Limit; }
        uint64_t Failures() const { return m_Failures.load(std::memory_order_relaxed); }
        uint64_t Allocations() const { return m_Count.load(std::memory_order_relaxed); }
        bool WentOver() const { return m_WentOver.load(std::memory_order_relaxed); }

    private:
        std::function<void*(size_t, size_t)> m_Allocate;
        std::function<void(void*, size_t, size_t)> m_Deallocate;
        uint64_t m_Limit;
        bool m_Enforce;
        std::atomic<uint64_t> m_Used{ 0 };
        std::atomic<uint64_t> m_Peak{ 0 };
        std::atomic<uint64_t> m_Failures{ 0 };
        std::atomic<uint64_t> m_Count{ 0 };
        std::atomic<bool> m_WentOver{ false };
    };

    template <typename T>
    class PluginAllocator {
    public:
        using value_type = T;
        explicit PluginAllocator(PluginMemory& memory) : m_Memory(&memory) {}
        template <typename U> PluginAllocator(const PluginAllocator<U>& other) : m_Memory(other.Memory()) {}
        T* allocate(size_t n)
        {
            void* p = m_Memory->Allocate(n * sizeof(T), alignof(T) < 16 ? 16 : alignof(T));
            if (!p) throw std::bad_alloc();
            return static_cast<T*>(p);
        }
        void deallocate(T* p, size_t) noexcept { m_Memory->Free(p); }
        PluginMemory* Memory() const { return m_Memory; }
        template <typename U> bool operator==(const PluginAllocator<U>& o) const { return m_Memory == o.Memory(); }
        template <typename U> bool operator!=(const PluginAllocator<U>& o) const { return m_Memory != o.Memory(); }
    private:
        PluginMemory* m_Memory;
    };

    class ComponentRegistrar {
    public:
        bool Register(const std::string& name, Objects::Components::ComponentFactory factory);

        template <typename T>
        bool Register(const std::string& name)
        {
            return Register(name, Objects::Components::ComponentFactory(+[]() -> std::shared_ptr<Objects::Components::Component> { return Core::Object::Create<T>(nullptr, 0); }));
        }

        size_t AdoptStaticRegistrars();

        size_t AdoptNewSince(const std::vector<std::string>& before);
        std::vector<std::string> Snapshot() const;

    private:
        friend class PluginContext;
        explicit ComponentRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        PluginContext& m_Ctx;
    };

    class SystemRegistrar {
    public:
        std::string Add(Core::Ecs::SystemDesc desc);
        std::string Add(Core::Ecs::SystemDesc desc, Core::Ecs::SystemFn fn);
        std::string NameOf(const std::string& name) const;
        bool SetEnabled(const std::string& name, bool enabled);
        bool Remove(const std::string& name);
        bool Has(const std::string& name) const;

    private:
        friend class PluginContext;
        explicit SystemRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        PluginContext& m_Ctx;
    };

    class EventRegistrar {
    public:
        template <typename T>
        void Subscribe(std::function<void(const T&)> callback)
        {
            auto cb = std::make_shared<std::function<void(const T&)>>(std::move(callback));
            Defer("event:" + std::string(typeid(T).name()), [cb](Events::EventDispatcher& d, Events::EventDispatcher::Owner owner) {
                d.subscribeGlobal<T>(*cb, owner);
            });
        }
        template <typename T>
        void SubscribeToWorld(int worldID, std::function<void(const T&)> callback)
        {
            auto cb = std::make_shared<std::function<void(const T&)>>(std::move(callback));
            Defer("event:world:" + std::string(typeid(T).name()), [cb, worldID](Events::EventDispatcher& d, Events::EventDispatcher::Owner owner) {
                d.subscribeToWorld<T>(worldID, *cb, owner);
            });
        }
        template <typename T>
        void Emit(const T& event) const { if (Events::EventDispatcher* d = Dispatcher()) d->emitGlobal(event); }

        uint64_t SubscribeNamed(const std::string& name, std::function<void(const Events::NamedEvent&)> callback);
        uint64_t SubscribeNamedToWorld(int worldID, const std::string& name, std::function<void(const Events::NamedEvent&)> callback);
        bool Unsubscribe(uint64_t id);
        bool DescribeNamed(const std::string& name, Events::NamedEventLayout layout);
        bool EmitNamed(const std::string& name, const void* payload, size_t size) const;
        bool EmitNamedToWorld(int worldID, const std::string& name, const void* payload, size_t size) const;

    private:
        friend class PluginContext;
        explicit EventRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        void Defer(std::string name, std::function<void(Events::EventDispatcher&, Events::EventDispatcher::Owner)> subscribe);
        uint64_t AddNamed(std::string name, std::function<Events::EventDispatcher::Subscription(Events::EventDispatcher&, Events::EventDispatcher::Owner)> subscribe);
        Events::EventDispatcher* Dispatcher() const;
        PluginContext& m_Ctx;

        struct NamedSubscription {
            Events::EventDispatcher::Subscription token = 0;
            bool cancelled = false;
        };
        std::vector<std::pair<uint64_t, std::shared_ptr<NamedSubscription>>> m_Named;
        uint64_t m_NextNamed = 0;
    };

    class CommandRegistrar {
    public:
        bool Add(const std::string& name, const std::string& help, Core::CVarRegistry::CommandFn fn);

    private:
        friend class PluginContext;
        explicit CommandRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        PluginContext& m_Ctx;
    };

    class CVarRegistrar {
    public:
        Core::CVar<bool>* Bool(const std::string& name, bool value, const std::string& help = {}, Core::CVarFlags flags = Core::CVarFlags::None);
        Core::CVar<int>* Int(const std::string& name, int value, const std::string& help = {}, Core::CVarFlags flags = Core::CVarFlags::None,
                             int min = std::numeric_limits<int>::lowest(), int max = std::numeric_limits<int>::max());
        Core::CVar<float>* Float(const std::string& name, float value, const std::string& help = {}, Core::CVarFlags flags = Core::CVarFlags::None,
                                 float min = std::numeric_limits<float>::lowest(), float max = std::numeric_limits<float>::max());
        Core::CVar<std::string>* String(const std::string& name, const std::string& value, const std::string& help = {}, Core::CVarFlags flags = Core::CVarFlags::None);

        std::string NameOf(const std::string& name) const;

    private:
        friend class PluginContext;
        explicit CVarRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        template <typename T, typename... Args> Core::CVar<T>* Make(const std::string& name, Args&&... args);
        PluginContext& m_Ctx;
    };

    class WorldRegistrar {
    public:
        template <typename T>
        bool RegisterExtension()
        {
            return AddExtension(std::type_index(typeid(T)), []() -> std::unique_ptr<Worlds::IWorldExtension> { return std::make_unique<T>(); });
        }
        void RegisterSettingsAssetRefs(Worlds::WorldSettingsAssetRefs refs);

    private:
        friend class PluginContext;
        explicit WorldRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        bool AddExtension(std::type_index type, Worlds::WorldExtensionFactory factory);
        PluginContext& m_Ctx;
    };

    class AssetRegistrar {
    public:
        template <typename T>
        bool Add(const std::string& point, const std::string& name, std::shared_ptr<T> item);
        std::string Uri(const std::string& path) const;
        Core::Guid GuidOf(const std::string& path) const;

    private:
        friend class PluginContext;
        explicit AssetRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        PluginContext& m_Ctx;
    };

    class RenderRegistrar {
    public:
        template <typename T>
        bool AddFeature(const std::string& name, std::shared_ptr<T> feature);
        template <typename T>
        bool AddPipeline(const std::string& name, std::shared_ptr<T> pipeline);

    private:
        friend class PluginContext;
        explicit RenderRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        PluginContext& m_Ctx;
    };

    class HookRegistrarApi {
    public:
        template <typename H>
        HookRegistry::Token On(Priority priority, std::function<void(H&)> handler);
        template <typename H>
        HookRegistry::Token On(std::function<void(H&)> handler) { return On<H>(Priority::Normal, std::move(handler)); }
        bool Off(HookRegistry::Token token);

    private:
        friend class PluginContext;
        explicit HookRegistrarApi(PluginContext& ctx) : m_Ctx(ctx) {}
        PluginContext& m_Ctx;
        std::vector<HookRegistry::Token> m_Tokens;
    };

    class ServiceRegistrar {
    public:
        template <typename T>
        bool Provide(const std::string& name, uint32_t versionMajor, T* instance, uint32_t versionMinor = 0);
        template <typename T>
        ServiceRef<T> Get(const std::string& name, const std::string& range = "*") const;
        bool Has(const std::string& name) const;

    private:
        friend class PluginContext;
        explicit ServiceRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        PluginContext& m_Ctx;
    };

    class PluginContext {
    public:
        PluginContext(const PluginManifest& manifest, PluginOwner owner, Host host, HostBindings bindings);
        ~PluginContext();
        PluginContext(const PluginContext&) = delete;
        PluginContext& operator=(const PluginContext&) = delete;

        const PluginManifest& Manifest() const { return *m_Manifest; }
        const std::string& Id() const { return m_Manifest->id; }
        PluginOwner Owner() const { return m_Owner; }
        Host CurrentHost() const { return m_Host; }
        const std::string& Directory() const { return m_Manifest->rootDir; }

        ComponentRegistrar& Components() { return m_Components; }
        SystemRegistrar& Systems() { return m_Systems; }
        EventRegistrar& Events() { return m_Events; }
        CommandRegistrar& Commands() { return m_Commands; }
        CVarRegistrar& CVars() { return m_CVars; }
        WorldRegistrar& Worlds() { return m_Worlds; }
        AssetRegistrar& Assets() { return m_Assets; }
        RenderRegistrar& Render() { return m_Render; }
        HookRegistrarApi& Hooks() { return m_Hooks; }
        ServiceRegistrar& Services() { return m_Services; }
        ContentMounts& Content() { return *m_B.content; }
        PluginMemory& Memory() { return m_Memory; }
        ExtensionRegistry& Extensions() { return *m_B.extensions; }
        Core::IEngineContext* Engine() const { return m_B.engine; }

        void Log(const std::string& message) const;
        void Warn(const std::string& message) const;
        void Error(const std::string& message) const;

        // Seeded from the run seed plus plugin and stream names, so adding a plugin does not change this one's numbers
        Core::Random MakeRandomStream(std::string_view name) const;

        bool Has(const std::string& permission) const;

        void BindEngine(Core::Ecs::Scheduler* scheduler, Events::EventDispatcher* events, Core::IEngineContext* engine = nullptr);
        void ReleaseAll();
        std::vector<std::pair<std::string, size_t>> Summary() const;
        bool Owns(const std::string& kind) const;
        const std::vector<std::string>& SystemNames() const { return m_SystemNames; }
        void OnRelease(const std::string& what, std::function<void()> release) { AddRecord("release", what, std::move(release)); }
        size_t RegistrationCount() const { return m_Records.size(); }

        const HostBindings& Bindings() const { return m_B; }

    private:
        friend class ComponentRegistrar;
        friend class SystemRegistrar;
        friend class EventRegistrar;
        friend class CommandRegistrar;
        friend class CVarRegistrar;
        friend class WorldRegistrar;
        friend class AssetRegistrar;
        friend class RenderRegistrar;
        friend class HookRegistrarApi;
        friend class ServiceRegistrar;

        struct Record {
            std::string kind;
            std::string name;
            std::function<void()> release;
        };
        void AddRecord(std::string kind, std::string name, std::function<void()> release);

        const PluginManifest* m_Manifest;
        PluginOwner m_Owner;
        Host m_Host;
        HostBindings m_B;

        std::vector<Record> m_Records;
        std::vector<std::string> m_SystemNames;
        std::vector<std::pair<std::string, Core::Ecs::SystemDesc>> m_PendingSystems;
        std::vector<std::pair<std::string, std::function<void(Events::EventDispatcher&, Events::EventDispatcher::Owner)>>> m_PendingEvents;
        bool m_Released = false;

        PluginMemory m_Memory;
        ComponentRegistrar m_Components{ *this };
        SystemRegistrar m_Systems{ *this };
        EventRegistrar m_Events{ *this };
        CommandRegistrar m_Commands{ *this };
        CVarRegistrar m_CVars{ *this };
        WorldRegistrar m_Worlds{ *this };
        AssetRegistrar m_Assets{ *this };
        RenderRegistrar m_Render{ *this };
        HookRegistrarApi m_Hooks{ *this };
        ServiceRegistrar m_Services{ *this };
    };

    template <typename T, typename... Args>
    Core::CVar<T>* CVarRegistrar::Make(const std::string& name, Args&&... args)
    {
        if (!m_Ctx.m_B.cvars) return nullptr;
        const std::string full = NameOf(name);
        if (m_Ctx.m_B.cvars->Find(full)) {
            m_Ctx.Warn("cvar '" + full + "' already exists");
            return nullptr;
        }
        auto* var = new Core::CVar<T>(*m_Ctx.m_B.cvars, full, std::forward<Args>(args)...);
        m_Ctx.AddRecord("cvar", full, [var]() { delete var; });
        return var;
    }

    template <typename T>
    bool AssetRegistrar::Add(const std::string& point, const std::string& name, std::shared_ptr<T> item)
    {
        if (!m_Ctx.m_B.extensions) return false;
        const PluginOwner owner = m_Ctx.m_Owner;
        ExtensionRegistry* registry = m_Ctx.m_B.extensions;
        if (!registry->Add<T>(point, name, std::move(item), owner)) return false;
        m_Ctx.AddRecord("extension", point + ":" + name, [registry, point, name, owner]() { registry->Remove(point, name, owner); });
        return true;
    }

    template <typename T>
    bool RenderRegistrar::AddFeature(const std::string& name, std::shared_ptr<T> feature)
    {
        return m_Ctx.m_Assets.Add<T>("render.feature", name, std::move(feature));
    }

    template <typename T>
    bool RenderRegistrar::AddPipeline(const std::string& name, std::shared_ptr<T> pipeline)
    {
        return m_Ctx.m_Assets.Add<T>("render.pipeline", name, std::move(pipeline));
    }

    template <typename H>
    HookRegistry::Token HookRegistrarApi::On(Priority priority, std::function<void(H&)> handler)
    {
        if (!m_Ctx.m_B.hooks) return 0;
        HookRegistry* registry = m_Ctx.m_B.hooks;
        const HookRegistry::Token token = registry->On<H>(priority, std::move(handler), m_Ctx.m_Owner);
        m_Ctx.AddRecord("hook", H::kName, [registry, token]() { registry->Remove(token); });
        m_Tokens.push_back(token);
        return token;
    }

    template <typename T>
    bool ServiceRegistrar::Provide(const std::string& name, uint32_t versionMajor, T* instance, uint32_t versionMinor)
    {
        if (!m_Ctx.m_B.services) return false;
        ServiceRegistry* registry = m_Ctx.m_B.services;
        const PluginOwner owner = m_Ctx.m_Owner;
        SemVer version;
        version.major = versionMajor;
        version.minor = versionMinor;
        if (!registry->Provide(name, version, static_cast<void*>(instance), owner)) {
            m_Ctx.Warn("service '" + name + "' is already provided by another plugin");
            return false;
        }
        m_Ctx.AddRecord("service", name, [registry, name, owner]() { registry->Revoke(name, owner); });
        return true;
    }

    template <typename T>
    ServiceRef<T> ServiceRegistrar::Get(const std::string& name, const std::string& range) const
    {
        return m_Ctx.m_B.services ? m_Ctx.m_B.services->Get<T>(name, range) : ServiceRef<T>();
    }

    inline bool AddEditorPanel(PluginContext& ctx, const std::string& title, std::function<void(bool* open)> draw)
    {
        auto item = std::make_shared<EditorPanelItem>();
        item->title = title;
        item->draw = std::move(draw);
        return ctx.Assets().Add<EditorPanelItem>("editor.panel", title, std::move(item));
    }
}
