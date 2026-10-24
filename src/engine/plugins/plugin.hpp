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
        Discovered,     ///< manifest read
        Resolved,       ///< dependencies met, place in the load order fixed
        Loaded,         ///< binary mapped, instance created
        Initialized,    ///< OnLoad and OnInitialize ran
        Enabled,        ///< running
        Disabled,       ///< stopped, still loaded
        Failed,         ///< see PluginRecord::error
        Unloaded,
    };
    const char* PluginStateName(PluginState state);

    /// What a plugin saves before a reload and gets back after (JSON, so reflection friendly and independent of the memory layout)
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

    /// @brief Base of everything that extends Shard. The host calls these in this order : OnLoad (every plugin), OnInitialize (every
    /// plugin), OnEnable ; and on the way out OnDisable, OnUnload.
    class IPlugin {
    public:
        virtual ~IPlugin() = default;

        /// The binary is loaded : register everything here (components, systems, cvars, commands, services, hooks...). Do not
        /// touch worlds, the GPU, or another plugin's services yet.
        virtual void OnLoad(PluginContext& ctx) {}

        /// Every plugin has run OnLoad : the services of the plugins you depend on can be asked for now.
        virtual void OnInitialize(PluginContext& ctx) {}

        /// The plugin starts running : its systems are on the schedule from the next frame.
        virtual void OnEnable(PluginContext& ctx) {}
        virtual void OnDisable(PluginContext& ctx) {}

        /// Last call before the binary goes away. Whatever was registered through `ctx` is removed by the host afterwards.
        virtual void OnUnload(PluginContext& ctx) {}

        /// Hot reload : write what must survive. Returning false makes the plugin not reloadable (it can still be disabled).
        virtual bool OnPrepareReload(StateWriter& out) { return false; }
        virtual void OnRestore(StateReader& in) {}
    };

    /// A plugin for the engine itself : renderer, physics, audio, platform, asset importers
    class EnginePlugin : public IPlugin {
    public:
        /// Start and end of every frame (main thread). Keep these short : the time they take is billed to the plugin.
        virtual void OnFrameBegin(uint64_t frame) {}
        virtual void OnFrameEnd(uint64_t frame) {}
    };

    /// @brief A window an editor plugin adds to the editor's Window menu : the "editor.panel" extension point. `draw` is called every frame
    /// while `visible`, between the editor's ImGui frame calls, with a flag the window's close button clears. The plugin must draw with
    /// the editor's ImGui (a plugin built with the editor, or one that links the same ImGui and got its context).
    struct EditorPanelItem {
        std::string title;
        std::function<void(bool* open)> draw;
        bool visible = false;
    };

    /// @brief An item of a menu of the editor's menu bar : the "editor.menu" extension point. `path` is the menu and the item, separated by '/'
    /// ("Tools/Bake/All levels") ; `shortcut` is only displayed.
    struct EditorMenuItem {
        std::string path;
        std::string shortcut;
        std::function<void()> run;
    };

    /// @brief A button in the viewport's toolbar : the "editor.toolbar" extension point
    struct EditorToolbarItem {
        std::string label;
        std::string tooltip;
        std::function<void()> run;
    };

    /// @brief What a plugin draws below a component in the Properties panel : the "editor.inspector" extension point. `componentType` is the name
    /// of the component as the world file writes it ("light", "game.Health"), empty : below all of them. `ui` is the editor's widgets, `actor` the handle of the
    /// actor shown (shard_actors.h).
    struct EditorInspectorItem {
        std::string componentType;
        std::function<void(const ShardUiApi* ui, uint64_t actor)> draw;
    };

    /// @brief A plugin opens the assets of an extension (".bt") : the "editor.assetopener" extension point. A double click in the content browser calls `open`
    /// with the path of the asset in the project.
    struct EditorAssetOpenerItem {
        std::string extension;
        std::function<void(const std::string& pathInProject)> open;
    };

    /// @brief An entry of the content browser's Create menu that writes a file with fixed bytes : the "editor.assettemplate" extension point
    struct EditorAssetTemplateItem {
        std::string label;
        std::string defaultName;
        std::string bytes;
    };

    /// @brief What a plugin draws in the viewport for each selected actor : the "editor.gizmo" extension point
    struct EditorGizmoItem {
        std::string name;
        std::function<void(const ShardGizmoApi* gizmo, uint64_t actor)> draw;
    };

    /// @brief A check a plugin adds to the asset validator : the "tool.validator" extension point. `validate` gets the project file and returns the problems it
    /// found, one line each (none : all is well).
    struct ToolValidatorItem {
        std::string name;
        std::function<std::vector<std::string>(const std::string& projectFile)> validate;
    };

    /// @brief The editor's Dear ImGui, for a plugin that draws windows : a library that links its own copy of ImGui has to be given the editor's context
    /// and allocators before it calls anything (see engine/plugins/editor_imgui.hpp, BindEditorImGui)
    struct EditorImGui {
        void* context = nullptr;                              ///< ImGuiContext*
        void* (*allocate)(size_t size, void* userData) = nullptr;
        void (*free)(void* pointer, void* userData) = nullptr;
        void* userData = nullptr;
    };

    /// What the editor hands to its plugins
    struct EditorContext {
        PluginContext& plugin;
        ExtensionRegistry& extensions;
        EditorImGui imgui;
    };

    /// A plugin that lives in the editor only : panels, gizmos, inspectors, importers, commands
    class EditorPlugin : public IPlugin {
    public:
        virtual void OnEditorInit(EditorContext& editor) {}
        virtual void OnEditorShutdown(EditorContext& editor) {}
        /// The names of the actors that are selected (empty : nothing)
        virtual void OnSelectionChanged(const std::vector<std::string>& actorPaths) {}
    };

    struct ClientInfo {
        uint32_t id = 0;
        std::string name;
        std::string address;
    };

    /// What a ServerPlugin answers to a client that is connecting
    struct ConnectDecision {
        bool allowed = true;
        std::string reason;
        void Refuse(std::string why) { allowed = false; reason = std::move(why); }
    };

    struct ServerContext {
        PluginContext& plugin;
        bool dedicated = false;         ///< ShardHeadless --server
    };

    /// A plugin for the headless host and dedicated servers : transports, administration, metrics, validation of clients
    class ServerPlugin : public IPlugin {
    public:
        virtual void OnServerStart(ServerContext& server) {}
        virtual void OnServerStop(ServerContext& server) {}
        virtual void OnClientConnect(ClientInfo& client, ConnectDecision& decision) {}
        virtual void OnClientDisconnect(const ClientInfo& client) {}
        virtual void OnServerTick(ServerContext& server, uint64_t tick) {}
    };

    /// A plugin for the offline tools (cooker, baker, asset validator)
    class ToolPlugin : public IPlugin {
    public:
        /// The tool is about to run `task` ("cook", "bake", "validate") on `project` ; return false to cancel it with a message
        virtual bool OnToolTask(const std::string& task, const std::string& project, std::string& message) { return true; }
    };

    /// A module : a plugin that brings content and game logic, and follows the worlds
    class IModule : public IPlugin {
    public:
        virtual ModuleKind Kind() const { return ModuleKind::Code; }

        /// The module's content folder is mounted (mod://<id>/) and readable
        virtual void OnContentMounted(PluginContext& ctx) {}

        /// A world finished loading (every actor exists) ; called for each world, in module load order
        virtual void OnWorldLoaded(Worlds::World& world) {}
        virtual void OnWorldUnloaded(Worlds::World& world) {}
        virtual void OnPlayStart(Worlds::World& world) {}
        virtual void OnPlayStop(Worlds::World& world) {}
    };

    // ---- What a plugin binary exports ----

    /// The pointers a library needs to talk to the executable that loaded it : the library carries its own copy of the engine's
    /// globals, so it is given the host's. Add a pointer here, never a new way of reaching a global.
    struct HostLinks {
        uint32_t structSize = sizeof(HostLinks);
        Core::IEngineContext* engine = nullptr;
        Objects::Components::ComponentRegistry* components = nullptr;
        Debugging::Logger* logger = nullptr;
        Core::TagRegistry* tags = nullptr;              ///< added after the first version : check structSize before reading it
    };

    struct PluginDesc {
        uint32_t structSize = sizeof(PluginDesc);
        uint32_t apiMajor = kPluginApiMajor;
        uint32_t apiMinor = kPluginApiMinor;
        const char* fingerprint = "";
        const char* id = "";                        ///< informative : the manifest is what the host trusts
        void (*link)(const HostLinks*) = nullptr;
        IPlugin* (*create)() = nullptr;
        void (*destroy)(IPlugin*) = nullptr;
    };

    using DescribeFn = const PluginDesc* (*)();

    /// Gives the library the host's globals (a no-op in the executable itself). Inline on purpose : it has to be compiled with the
    /// library's own BUILD_GAME, not the engine's.
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

    /// @brief Plugins that are part of the executable (the engine's own modules) : they register a factory and a manifest instead of
    /// being found on disk. Use SHARD_STATIC_PLUGIN, or call this from the application's startup (a static library may drop a
    /// translation unit nothing references).
    using PluginFactory = IPlugin* (*)();
    void RegisterStaticPlugin(const char* manifestJson, PluginFactory factory);
}

/// Exports `shard_plugin_describe` for a plugin class (the host calls it once, right after loading the library).
/// `linkFn` : a `void()` the library runs after it got the host's globals (nullptr for none), for the copies of other libraries'
/// statics it carries, such as Jolt's allocator.
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

/// Declares a plugin compiled into the executable. `manifestJson` is the manifest as a string literal.
#define SHARD_STATIC_PLUGIN(ClassName, manifestJson)                                                                    \
    namespace {                                                                                                         \
        ::Shard::Engine::Plugins::IPlugin* ShardCreateStatic_##ClassName() { return new ClassName(); }                  \
        struct ShardStaticRegistrar_##ClassName {                                                                       \
            ShardStaticRegistrar_##ClassName() { ::Shard::Engine::Plugins::RegisterStaticPlugin(manifestJson, &ShardCreateStatic_##ClassName); } \
        } shardStaticRegistrar_##ClassName;                                                                             \
    }
