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

    /// Capabilities a plugin can ask for in its manifest (`permissions`). Native plugins are not held to them (see the docs,
    /// "Trust levels") : they tell the user what the plugin means to do, and sandboxed plugins are refused what they did not ask.
    namespace Permissions {
        inline constexpr const char* kFsReadContent = "fs.read:content";
        inline constexpr const char* kFsReadProject = "fs.read:project";
        inline constexpr const char* kFsWriteSave = "fs.write:save";
        inline constexpr const char* kNetwork = "net";
        inline constexpr const char* kProcess = "process";
        inline constexpr const char* kRenderFeature = "render.feature";
        inline constexpr const char* kEngineInternal = "engine.internal";
    }

    /// @brief The objects of the host a context reaches. All of them belong to the executable that loaded the plugin : a plugin's
    /// binary only holds pointers to them, never its own copy.
    struct HostBindings {
        Core::IEngineContext* engine = nullptr;
        Core::Ecs::Scheduler* scheduler = nullptr;                      ///< null until the engine has built its systems
        Events::EventDispatcher* events = nullptr;                      ///< idem
        Objects::Components::ComponentRegistry* components = nullptr;
        Core::CVarRegistry* cvars = nullptr;
        ServiceRegistry* services = nullptr;
        HookRegistry* hooks = nullptr;
        ExtensionRegistry* extensions = nullptr;
        ContentMounts* content = nullptr;
        Debugging::Logger* logger = nullptr;

        /// The host's allocator, for PluginContext::Memory() : memory that crosses between the plugin and the host comes from one place
        std::function<void*(size_t size, size_t align)> allocate;
        std::function<void(void* pointer, size_t size, size_t align)> deallocate;

        // Registries that live in process-wide statics (world_extension.cpp) : a plugin's binary carries its own copy of those, so it
        // goes through the host's functions instead of calling them
        /// Set to the plugin's owner id around every call into its systems, so a crash report can name the suspect ; null : not tracked
        std::atomic<uint32_t>* activePlugin = nullptr;
        /// A system of the plugin threw : told to the host, which decides whether to keep the plugin
        std::function<void(PluginOwner, const std::string&)> reportError;

        std::function<bool(std::type_index, Worlds::WorldExtensionFactory)> registerWorldExtension;
        std::function<bool(std::type_index)> unregisterWorldExtension;
        std::function<uint32_t(Worlds::WorldSettingsAssetRefs)> registerSettingsRefs;
        std::function<bool(uint32_t)> unregisterSettingsRefs;
    };

    class PluginContext;

    /// @brief Memory the plugin takes from the host's allocator and the host counts : it is what the manifest's `budgets.memoryMB` is measured against.
    /// Use it for the big things a plugin owns (caches, buffers), directly or through PluginAllocator for the standard containers. Memory taken any
    /// other way is not counted. What is still allocated when the plugin is unloaded is reported as a leak.
    class PluginMemory {
    public:
        PluginMemory(const HostBindings& host, uint64_t limitBytes, bool enforce) : m_Allocate(host.allocate), m_Deallocate(host.deallocate), m_Limit(limitBytes), m_Enforce(enforce) {}

        /// @return null if the plugin enforces its budget and this would pass it (counted in Failures())
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
        /// True once the plugin went over its limit (enforced or not)
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

    /// A standard allocator over PluginMemory : `std::vector<int, PluginAllocator<int>> v{PluginAllocator<int>(ctx.Memory())};`
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

    // ---- Registrars : the parts of a context ----

    class ComponentRegistrar {
    public:
        /// Registers a component type under `name` (the name its serialized form carries) ; "<id>/<name>" is the convention
        bool Register(const std::string& name, Objects::Components::ComponentFactory factory);

        template <typename T>
        bool Register(const std::string& name)
        {
            return Register(name, Objects::Components::ComponentFactory(+[]() -> std::shared_ptr<Objects::Components::Component> { return Core::Object::Create<T>(nullptr, 0); }));
        }

        /// @brief Runs the static registrars the old way of declaring components leaves (REGISTER_COMPONENT) and takes ownership of
        /// the types they added, so that they leave with the plugin.
        size_t AdoptStaticRegistrars();

        /// Takes ownership of every component type that is registered now and was not in `before` (names of the registry
        /// at some earlier point) : for libraries that register components themselves.
        size_t AdoptNewSince(const std::vector<std::string>& before);
        /// Names of the component types registered now, sorted
        std::vector<std::string> Snapshot() const;

    private:
        friend class PluginContext;
        explicit ComponentRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        PluginContext& m_Ctx;
    };

    class SystemRegistrar {
    public:
        /// @brief Adds an ECS system. Its name becomes "<plugin id>/<name>", and a name written "./x" in after / before means this plugin's "x".
        /// When the engine's scheduler does not exist yet, the system is added as soon as it does.
        /// @return the name it was registered under
        std::string Add(Core::Ecs::SystemDesc desc);
        std::string Add(Core::Ecs::SystemDesc desc, Core::Ecs::SystemFn fn);
        /// The full name of one of this plugin's systems
        std::string NameOf(const std::string& name) const;
        bool SetEnabled(const std::string& name, bool enabled);

    private:
        friend class PluginContext;
        explicit SystemRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        PluginContext& m_Ctx;
    };

    class EventRegistrar {
    public:
        /// Listens to an event sent globally. The subscription ends with the plugin.
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
        /// Sends an event globally (nothing if the engine has no dispatcher yet)
        template <typename T>
        void Emit(const T& event) const { if (Events::EventDispatcher* d = Dispatcher()) d->emitGlobal(event); }

    private:
        friend class PluginContext;
        explicit EventRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        void Defer(std::string name, std::function<void(Events::EventDispatcher&, Events::EventDispatcher::Owner)> subscribe);
        Events::EventDispatcher* Dispatcher() const;
        PluginContext& m_Ctx;
    };

    class CommandRegistrar {
    public:
        /// A console command, named "<name>" as given (use a prefix of your own : "weather.set")
        bool Add(const std::string& name, const std::string& help, Core::CVarRegistry::CommandFn fn);

    private:
        friend class PluginContext;
        explicit CommandRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        PluginContext& m_Ctx;
    };

    class CVarRegistrar {
    public:
        /// All of these create a variable named "<plugin id>.<name>" and return it. The variable lives as long as the plugin.
        Core::CVar<bool>* Bool(const std::string& name, bool value, const std::string& help = {}, Core::CVarFlags flags = Core::CVarFlags::None);
        Core::CVar<int>* Int(const std::string& name, int value, const std::string& help = {}, Core::CVarFlags flags = Core::CVarFlags::None,
                             int min = std::numeric_limits<int>::lowest(), int max = std::numeric_limits<int>::max());
        Core::CVar<float>* Float(const std::string& name, float value, const std::string& help = {}, Core::CVarFlags flags = Core::CVarFlags::None,
                                 float min = std::numeric_limits<float>::lowest(), float max = std::numeric_limits<float>::max());
        Core::CVar<std::string>* String(const std::string& name, const std::string& value, const std::string& help = {}, Core::CVarFlags flags = Core::CVarFlags::None);

        /// The full name a variable of this plugin has
        std::string NameOf(const std::string& name) const;

    private:
        friend class PluginContext;
        explicit CVarRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        template <typename T, typename... Args> Core::CVar<T>* Make(const std::string& name, Args&&... args);
        PluginContext& m_Ctx;
    };

    class WorldRegistrar {
    public:
        /// Attaches a T to every world (see IWorldExtension)
        template <typename T>
        bool RegisterExtension()
        {
            return AddExtension(std::type_index(typeid(T)), []() -> std::unique_ptr<Worlds::IWorldExtension> { return std::make_unique<T>(); });
        }
        /// Assets the `settings` object of a world file references
        void RegisterSettingsAssetRefs(Worlds::WorldSettingsAssetRefs refs);

    private:
        friend class PluginContext;
        explicit WorldRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        bool AddExtension(std::type_index type, Worlds::WorldExtensionFactory factory);
        PluginContext& m_Ctx;
    };

    class AssetRegistrar {
    public:
        /// An item for an extension point of a subsystem : an importer ("asset.importer"), an asset type ("asset.type")... The item's
        /// type is defined by whoever owns the point.
        template <typename T>
        bool Add(const std::string& point, const std::string& name, std::shared_ptr<T> item);
        /// The mod:// address of a file of this plugin
        std::string Uri(const std::string& path) const;
        /// The stable id of one of this plugin's files
        Core::Guid GuidOf(const std::string& path) const;

    private:
        friend class PluginContext;
        explicit AssetRegistrar(PluginContext& ctx) : m_Ctx(ctx) {}
        PluginContext& m_Ctx;
    };

    class RenderRegistrar {
    public:
        /// A render feature or pipeline for the renderer to pick up : "render.feature" / "render.pipeline". A feature reads the frame's
        /// snapshot, never the world.
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
        /// Intercepts a hook of Hooks::
        template <typename H>
        void On(Priority priority, std::function<void(H&)> handler);
        template <typename H>
        void On(std::function<void(H&)> handler) { On<H>(Priority::Normal, std::move(handler)); }

    private:
        friend class PluginContext;
        explicit HookRegistrarApi(PluginContext& ctx) : m_Ctx(ctx) {}
        PluginContext& m_Ctx;
    };

    class ServiceRegistrar {
    public:
        /// Offers `instance` as service `name`. It stays valid until the plugin is disabled.
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

    /// @brief What a plugin sees of the host, and the only way it adds anything to it. Every registration is tagged with the plugin so
    /// that the host removes all of it when the plugin is disabled or reloaded.
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
        /// The folder of the plugin on disk
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
        /// Counted memory, measured against the manifest's budget
        PluginMemory& Memory() { return m_Memory; }
        ExtensionRegistry& Extensions() { return *m_B.extensions; }
        Core::IEngineContext* Engine() const { return m_B.engine; }

        /// Writes to the log, tagged with the plugin id
        void Log(const std::string& message) const;
        void Warn(const std::string& message) const;
        void Error(const std::string& message) const;

        /// @brief A random stream of this plugin, seeded from the run's seed and the names of the plugin and the stream : adding another plugin
        /// does not change the numbers this one draws.
        Core::Random MakeRandomStream(std::string_view name) const;

        /// True if the plugin asked for `permission` in its manifest
        bool Has(const std::string& permission) const;

        // ---- Used by the plugin manager ----
        /// The engine's scheduler and dispatcher exist now : adds what was waiting for them
        void BindEngine(Core::Ecs::Scheduler* scheduler, Events::EventDispatcher* events);
        /// Removes everything the plugin registered, last registered first. Safe to call twice.
        void ReleaseAll();
        /// How many registrations of each kind are live : for the plugin listing
        std::vector<std::pair<std::string, size_t>> Summary() const;
        /// True when at least one registration of `kind` ("component", "system"...) is live
        bool Owns(const std::string& kind) const;
        /// Names of the systems this plugin registered (full names)
        const std::vector<std::string>& SystemNames() const { return m_SystemNames; }
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

    // ---- Templates that need the full context ----

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
    void HookRegistrarApi::On(Priority priority, std::function<void(H&)> handler)
    {
        if (!m_Ctx.m_B.hooks) return;
        HookRegistry* registry = m_Ctx.m_B.hooks;
        const HookRegistry::Token token = registry->On<H>(priority, std::move(handler), m_Ctx.m_Owner);
        m_Ctx.AddRecord("hook", H::kName, [registry, token]() { registry->Remove(token); });
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

    /// Adds a window to the editor's Window menu (see EditorPanelItem). Call it from EditorPlugin::OnEditorInit.
    inline bool AddEditorPanel(PluginContext& ctx, const std::string& title, std::function<void(bool* open)> draw)
    {
        auto item = std::make_shared<EditorPanelItem>();
        item->title = title;
        item->draw = std::move(draw);
        return ctx.Assets().Add<EditorPanelItem>("editor.panel", title, std::move(item));
    }
}
