#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/core/diagnostics/logger.hpp"
#include "engine/plugins/fingerprint.hpp"
#include "engine/plugins/hooks.hpp"
#include "engine/plugins/manifest.hpp"
#include "engine/core/tags/tag_registry.hpp"
#include "engine/world/components/registry/component_registry.hpp"
#include "engine/world/engine.hpp"

namespace Shard::Engine::Core { class IEngineContext; }
namespace Shard::Engine::Debugging { class Logger; }
namespace Shard::Engine::Objects::Components { class ComponentRegistry; }
namespace Shard::Engine::Worlds { class World; }

#if defined(_WIN32) || defined(_WIN64)
#   define SHARD_PLUGIN_EXPORT __declspec(dllexport)
#else
#   define SHARD_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

struct ShardUiApi;
struct ShardGizmoApi;

namespace Shard::Engine::Plugins {

    class PluginContext;
    class ExtensionRegistry;
    class ContentMounts;

    enum class PluginState : uint8_t {
        Discovered,
        Resolved,
        Loaded,
        Initialized,
        Enabled,
        Disabled,
        Failed,
        Unloaded,
    };
    const char* PluginStateName(PluginState state);

    class StateWriter {
    public:
        template <typename T> void Set(const std::string& key, const T& value) { m_Data[key] = value; }
        void SetJson(const std::string& key, nlohmann::json value) { m_Data[key] = std::move(value); }
        const nlohmann::json& Data() const { return m_Data; }
    private:
        nlohmann::json m_Data = nlohmann::json::object();
    };

    class StateReader {
    public:
        explicit StateReader(const nlohmann::json& data) : m_Data(data) {}
        bool Has(const std::string& key) const { return m_Data.contains(key); }
        template <typename T> T Get(const std::string& key, T fallback = T()) const
        {
            if (!m_Data.contains(key)) return fallback;
            try { return m_Data[key].get<T>(); } catch (...) { return fallback; }
        }
        const nlohmann::json& GetJson(const std::string& key) const { static const nlohmann::json null; return m_Data.contains(key) ? m_Data[key] : null; }
    private:
        const nlohmann::json& m_Data;
    };

    class IPlugin {
    public:
        virtual ~IPlugin() = default;

        // Register everything here. Do not touch worlds, the GPU or other plugins' services yet.
        virtual void OnLoad(PluginContext& ctx) {}

        virtual void OnInitialize(PluginContext& ctx) {}

        virtual void OnEnable(PluginContext& ctx) {}
        virtual void OnDisable(PluginContext& ctx) {}

        virtual void OnUnload(PluginContext& ctx) {}

        virtual bool OnPrepareReload(StateWriter& out) { return false; }
        virtual void OnRestore(StateReader& in) {}
    };

    class EnginePlugin : public IPlugin {
    public:
        // Main thread. Keep short, the time is billed to the plugin.
        virtual void OnFrameBegin(uint64_t frame) {}
        virtual void OnFrameEnd(uint64_t frame) {}
    };

    struct EditorPanelItem {
        std::string title;
        std::function<void(bool* open)> draw;
        bool visible = false;
    };

    struct EditorMenuItem {
        std::string path;
        std::string shortcut;
        std::function<void()> run;
    };

    struct EditorToolbarItem {
        std::string label;
        std::string tooltip;
        std::function<void()> run;
    };

    struct EditorInspectorItem {
        std::string componentType;
        std::function<void(const ShardUiApi* ui, uint64_t actor)> draw;
    };

    struct EditorAssetOpenerItem {
        std::string extension;
        std::function<void(const std::string& pathInProject)> open;
    };

    struct EditorAssetTemplateItem {
        std::string label;
        std::string defaultName;
        std::string bytes;
    };

    struct EditorGizmoItem {
        std::string name;
        std::function<void(const ShardGizmoApi* gizmo, uint64_t actor)> draw;
    };

    struct ToolValidatorItem {
        std::string name;
        std::function<std::vector<std::string>(const std::string& projectFile)> validate;
    };

    struct EditorImGui {
        void* context = nullptr;
        void* (*allocate)(size_t size, void* userData) = nullptr;
        void (*free)(void* pointer, void* userData) = nullptr;
        void* userData = nullptr;
    };

    struct EditorContext {
        PluginContext& plugin;
        ExtensionRegistry& extensions;
        EditorImGui imgui;
    };

    class EditorPlugin : public IPlugin {
    public:
        virtual void OnEditorInit(EditorContext& editor) {}
        virtual void OnEditorShutdown(EditorContext& editor) {}
        virtual void OnSelectionChanged(const std::vector<std::string>& actorPaths) {}
    };

    struct ClientInfo {
        uint32_t id = 0;
        std::string name;
        std::string address;
    };

    struct ConnectDecision {
        bool allowed = true;
        std::string reason;
        void Refuse(std::string why) { allowed = false; reason = std::move(why); }
    };

    struct ServerContext {
        PluginContext& plugin;
        bool dedicated = false;
    };

    class ServerPlugin : public IPlugin {
    public:
        virtual void OnServerStart(ServerContext& server) {}
        virtual void OnServerStop(ServerContext& server) {}
        virtual void OnClientConnect(ClientInfo& client, ConnectDecision& decision) {}
        virtual void OnClientDisconnect(const ClientInfo& client) {}
        virtual void OnServerTick(ServerContext& server, uint64_t tick) {}
    };

    class ToolPlugin : public IPlugin {
    public:
        virtual bool OnToolTask(const std::string& task, const std::string& project, std::string& message) { return true; }
    };

    class IModule : public IPlugin {
    public:
        virtual ModuleKind Kind() const { return ModuleKind::Code; }

        virtual void OnContentMounted(PluginContext& ctx) {}

        virtual void OnWorldLoaded(Worlds::World& world) {}
        virtual void OnWorldUnloaded(Worlds::World& world) {}
        virtual void OnPlayStart(Worlds::World& world) {}
        virtual void OnPlayStop(Worlds::World& world) {}
    };

    // A library carries its own copy of the engine's globals, so it is given the host's. Add a pointer here, never another way to reach a global.
    struct HostLinks {
        uint32_t structSize = sizeof(HostLinks);
        Core::IEngineContext* engine = nullptr;
        Objects::Components::ComponentRegistry* components = nullptr;
        Debugging::Logger* logger = nullptr;
        Core::TagRegistry* tags = nullptr;
    };

    struct PluginDesc {
        uint32_t structSize = sizeof(PluginDesc);
        uint32_t apiMajor = kPluginApiMajor;
        uint32_t apiMinor = kPluginApiMinor;
        const char* fingerprint = "";
        const char* id = "";
        void (*link)(const HostLinks*) = nullptr;
        IPlugin* (*create)() = nullptr;
        void (*destroy)(IPlugin*) = nullptr;
    };

    using DescribeFn = const PluginDesc* (*)();

    // Inline on purpose: must be compiled with the library's own BUILD_GAME, not the engine's
    inline void LinkHostGlobals(const HostLinks* links)
    {
#if defined(BUILD_GAME) && !defined(BUILD_ENGINE)
        Core::SetEngine(links->engine);
        Objects::Components::SetComponentRegistry(links->components);
        Debugging::SetLogger(links->logger);
        if (links->structSize >= offsetof(HostLinks, tags) + sizeof(links->tags))
            Core::TagRegistry::Link(links->tags);
#else
        (void)links;
#endif
    }

    using PluginFactory = IPlugin* (*)();
    void RegisterStaticPlugin(const char* manifestJson, PluginFactory factory);
}

// Called once right after loading. linkFn runs after the host globals are set, for copies of other libraries' statics (e.g. Jolt's allocator).
#define SHARD_PLUGIN_WITH_LINK(ClassName, linkFn)                                                                      \
    namespace {                                                                                                         \
        ::Shard::Engine::Plugins::IPlugin* ShardCreatePlugin_() { return new ClassName(); }                             \
        void ShardDestroyPlugin_(::Shard::Engine::Plugins::IPlugin* plugin) { delete plugin; }                          \
        void ShardLinkPlugin_(const ::Shard::Engine::Plugins::HostLinks* links)                                         \
        {                                                                                                               \
            ::Shard::Engine::Plugins::LinkHostGlobals(links);                                                           \
            void (*fn)() = linkFn;                                                                                      \
            if (fn) fn();                                                                                               \
        }                                                                                                               \
        const ::Shard::Engine::Plugins::PluginDesc kShardPluginDesc_ = {                                                \
            sizeof(::Shard::Engine::Plugins::PluginDesc), ::Shard::Engine::Plugins::kPluginApiMajor,                    \
            ::Shard::Engine::Plugins::kPluginApiMinor, SHARD_BUILD_FINGERPRINT, #ClassName,                             \
            &ShardLinkPlugin_, &ShardCreatePlugin_, &ShardDestroyPlugin_ };                                             \
    }                                                                                                                   \
    extern "C" SHARD_PLUGIN_EXPORT const ::Shard::Engine::Plugins::PluginDesc* shard_plugin_describe()                  \
    {                                                                                                                   \
        return &kShardPluginDesc_;                                                                                      \
    }

#define SHARD_PLUGIN(ClassName) SHARD_PLUGIN_WITH_LINK(ClassName, nullptr)

#define SHARD_STATIC_PLUGIN(ClassName, manifestJson)                                                                    \
    namespace {                                                                                                         \
        ::Shard::Engine::Plugins::IPlugin* ShardCreateStatic_##ClassName() { return new ClassName(); }                  \
        struct ShardStaticRegistrar_##ClassName {                                                                       \
            ShardStaticRegistrar_##ClassName() { ::Shard::Engine::Plugins::RegisterStaticPlugin(manifestJson, &ShardCreateStatic_##ClassName); } \
        } shardStaticRegistrar_##ClassName;                                                                             \
    }
