#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/platform/lib_loader/dynlib.hpp"
#include "engine/plugins/content.hpp"
#include "engine/plugins/context.hpp"
#include "engine/plugins/extensions.hpp"
#include "engine/plugins/hooks.hpp"
#include "engine/plugins/manifest.hpp"
#include "engine/plugins/package.hpp"
#include "engine/plugins/plugin.hpp"
#include "engine/plugins/registry.hpp"
#include "engine/plugins/resolver.hpp"
#include "engine/plugins/services.hpp"

namespace Shard::Engine::Core { class IEngineContext; }

namespace Shard::Engine::Plugins {

    /// Everything the manager knows about one plugin
    struct PluginRecord {
        PluginManifest manifest;
        PluginState state = PluginState::Discovered;
        std::string error;                              ///< why it is Failed
        std::vector<std::string> warnings;
        PluginOwner owner = 0;                          ///< 1-based position in the load order
        bool required = false;                          ///< the project cannot do without it
        bool isStatic = false;                          ///< compiled into the executable
        bool legacy = false;                            ///< a library of the old kind (InitializeSingletons / RegisterGameComponents)

        IPlugin* instance = nullptr;
        EnginePlugin* enginePlugin = nullptr;           ///< instance as the kind it is, null if it is not one
        EditorPlugin* editorPlugin = nullptr;
        ServerPlugin* serverPlugin = nullptr;
        ToolPlugin* toolPlugin = nullptr;
        IModule* module = nullptr;
        const PluginDesc* desc = nullptr;
        PluginFactory staticFactory = nullptr;
        Core::Platform::DynLib library;
        std::string loadedFrom;                         ///< the file the library was really loaded from (a copy, when hot reload is on)
        std::unique_ptr<PluginContext> context;

        SignatureInfo signature;                        ///< who signed the folder (Unsigned when nobody did)
        bool installed = false;                         ///< put there by an install from a registry : it has to carry a trusted signature
        std::string sourceArchive;                      ///< the .shardplugin it was unpacked from, if any

        uint64_t binaryHash = 0;                        ///< of the library file
        uint64_t contentHash = 0;                       ///< of the content folder (only worked out for plugins that affect the simulation)
        uint64_t identityHash = 0;                      ///< what a replay or a server compares : id, version, binary, content

        // Cost
        double frameMsAverage = 0.0;                    ///< OnFrameBegin / OnFrameEnd, moving average
        double systemsMsAverage = 0.0;                  ///< its systems, from the scheduler
        uint32_t errorCount = 0;                        ///< exceptions caught in its systems and callbacks
        bool overBudget = false;
        double memoryMB = 0.0;                          ///< counted memory (PluginContext::Memory()) in use
        bool overMemoryBudget = false;
        uint32_t reloadCount = 0;

        bool IsRunning() const { return state == PluginState::Enabled; }
        bool IsLive() const { return state == PluginState::Initialized || state == PluginState::Enabled || state == PluginState::Disabled; }
    };

    /// One line of a plugin set : what has to match between two machines running the same simulation
    struct PluginSetEntry {
        std::string id;
        std::string version;
        uint64_t hash = 0;
    };

    struct PluginSetDiff {
        std::vector<std::string> missing;               ///< the other side has them, this side does not
        std::vector<std::string> extra;                 ///< this side has them, the other does not
        std::vector<std::string> different;             ///< same id, other version or other content
        bool Same() const { return missing.empty() && extra.empty() && different.empty(); }
        std::string Describe() const;
    };

    /// Compares what is `local` here with what `remote` (a replay, a server) says
    PluginSetDiff ComparePluginSets(const std::vector<PluginSetEntry>& local, const std::vector<PluginSetEntry>& remote);
    nlohmann::json PluginSetToJson(const std::vector<PluginSetEntry>& set);
    std::vector<PluginSetEntry> PluginSetFromJson(const nlohmann::json& json);
    uint64_t HashPluginSet(const std::vector<PluginSetEntry>& set);
    /// "<id> <version> <hash>" lines, what a replay file stores
    std::vector<std::string> PluginSetToLines(const std::vector<PluginSetEntry>& set);
    std::vector<PluginSetEntry> PluginSetFromLines(const std::vector<std::string>& lines);

    /// A component declared by a datapack (components/*.json). The engine does not build components out of these ; they are listed for
    /// the code that reads them and for the editor.
    struct DeclaredComponentEntry {
        std::string pluginId;
        DeclaredComponent component;
    };

    struct PluginManagerConfig {
        Host host = Host::Player;
        /// Folders that hold plugin folders (or are one) : the project's plugins folder, the user's, the engine's
        std::vector<std::string> searchPaths;
        /// --plugin : a plugin folder, a shard-plugin.json, or a library (which gets a manifest of its own, kind module / code)
        std::vector<std::string> pluginPaths;
        /// The game's library (--game) : loaded as the plugin "game", a code module, whether it exports SHARD_PLUGIN or the old entry points
        std::string gameLibrary;
        /// The project file : its "plugins" list and "pluginsFolder" are read
        std::string projectFile;
        SemVer engineVersion;
        std::set<std::string> extraFeatures;
        std::set<std::string> disabled;                 ///< --disable-plugin
        bool deterministic = false;                     ///< no hot reload, no copy of libraries
        bool readOnly = false;                          ///< do not write plugins.lock
        bool strictLock = false;                        ///< a plugins.lock that does not match is an error
        bool allowHotReload = false;
        bool ignoreSafeMode = false;
        std::string lockfilePath;                       ///< default : plugins.lock next to the project file
        std::string stateDirectory;                     ///< where the safe mode marker goes (default : .cache next to the project file)
        bool registerConsoleCommands = true;

        // ---- Trust and distribution ----
        /// Files of trusted publishers ({"publishers": {"name": "<key hex>"}}). Default : plugin-trust.json next to the project file.
        std::vector<std::string> trustFiles;
        /// Where .shardplugin archives are unpacked. Default : plugin_cache in the state directory (the temp folder without one).
        std::string cacheDirectory;
        /// Registries (an index.json, a folder, or an http:// URL) : the project's "registries" and SHARD_PLUGIN_REGISTRY are added by StartPlugins
        std::vector<std::string> registries;
        /// Where `plugin.install` puts plugins. Default : the project's plugins folder.
        std::string installDirectory;
        /// Load a native plugin (tier 1) that carries no valid signature of a trusted publisher, instead of refusing it
        bool allowUnsigned = false;
    };

    /// @brief Finds, checks, loads, runs and unloads the plugins of an executable.
    ///
    ///   Startup()            discover > resolve > load > OnLoad > OnInitialize > mount content   (before the engine starts)
    ///   OnEngineSystemsReady the scheduler and the event dispatcher exist : the systems and subscriptions that waited are added
    ///   OnEngineInitialized  OnEnable on every plugin, the worlds that are loaded already are announced to the modules
    ///   BeginFrame/EndFrame  per frame work, billed to the plugin
    ///   Shutdown()           OnDisable > OnUnload > everything the plugin registered is removed > the library goes
    ///
    /// One instance per process (Get()). Everything runs on the main thread.
    class PluginManager {
    public:
        static PluginManager& Get();

        // ---- Before Startup ----
        /// A plugin compiled into the executable : the engine's own modules, or a plugin of the application. Ignored once started.
        void AddStaticPlugin(const std::string& manifestJson, PluginFactory factory);

        /// Callbacks the application gives for the operations that touch the whole engine
        struct Callbacks {
            /// Called before a plugin's library is unloaded for a reload and after it is back : the worlds (whose components have code in the
            /// library) are put aside and rebuilt. Without them a reload is refused while a world is loaded.
            std::function<bool(std::string& error)> suspendWorlds;
            std::function<void()> resumeWorlds;
            /// Names of the worlds loaded now (project paths), for the module callbacks
            std::function<bool()> hasLoadedWorlds;
        };
        void SetCallbacks(Callbacks callbacks) { m_Callbacks = std::move(callbacks); }

        /// @return false if a required plugin could not load, or the plugins.lock was strict and did not match (`error` says why)
        bool Startup(const PluginManagerConfig& config, std::string* error = nullptr);
        bool IsStarted() const { return m_Started; }

        // ---- Engine lifecycle ----
        void OnEngineSystemsReady(Core::IEngineContext* engine, Core::Ecs::Scheduler* scheduler, Events::EventDispatcher* events);
        void OnEngineInitialized();
        void BeginFrame(uint64_t frame);
        void EndFrame(uint64_t frame);
        /// @return false if a plugin cancelled the play mode (PrePlay)
        bool BeforePlay();
        void AfterPlay();
        /// @return false if a plugin cancelled the load
        bool BeforeWorldLoad(const std::string& worldName);
        void AfterWorldLoad(const std::string& worldName);
        bool BeforeSave(const std::string& what);
        /// The engine is about to tear down its systems : the worlds are gone, the plugins stop and give back everything they registered
        /// (their systems and subscriptions point into objects that are about to be deleted)
        void OnEngineDestroying();
        /// Unloads the libraries. Call after the engine is destroyed.
        void Shutdown();

        // ---- Control ----
        bool Enable(const std::string& id, std::string* error = nullptr);
        /// Stops a plugin and removes everything it registered. With `cascade`, the plugins that depend on it stop first ; without, it is refused if any does.
        /// A plugin that registered components (itself or through the ones that depend on it) is refused while a world is loaded : the
        /// world holds instances of them. Unload the worlds first, or use Reload, which sets them aside.
        bool Disable(const std::string& id, bool cascade = true, std::string* error = nullptr);
        /// Rebuilds a plugin from its library on disk (dev builds, outside deterministic runs)
        bool Reload(const std::string& id, std::string* error = nullptr);

        // ---- Queries ----
        const std::vector<std::unique_ptr<PluginRecord>>& Plugins() const { return m_Records; }
        PluginRecord* Find(const std::string& id);
        const std::vector<Rejection>& Rejections() const { return m_Rejections; }
        const PluginManagerConfig& Config() const { return m_Config; }

        HookRegistry& Hooks() { return m_Hooks; }
        ServiceRegistry& Services() { return m_Services; }
        ExtensionRegistry& Extensions() { return m_Extensions; }
        ContentMounts& Content() { return m_Content; }
        const std::vector<DeclaredComponentEntry>& DeclaredComponents() const { return m_Declared; }

        /// Features of this engine build (what `engine.requires` names)
        const std::set<std::string>& EngineFeatures() const { return m_Features; }

        /// The plugins that change the simulation and are running, in load order : what a replay stores and a server announces
        std::vector<PluginSetEntry> SimulationPluginSet() const;
        uint64_t SimulationPluginHash() const { return HashPluginSet(SimulationPluginSet()); }

        /// A server asks a client's plugin set (JSON, from PluginSetToJson) ; refuses the client if it differs
        void CheckClientPlugins(const nlohmann::json& clientSet, ConnectDecision& decision) const;
        /// ServerPlugin callbacks, for the host that runs the clients
        void NotifyServerStart(bool dedicated);
        void NotifyServerStop(bool dedicated);
        void NotifyClientConnect(ClientInfo& client, ConnectDecision& decision);
        void NotifyClientDisconnect(const ClientInfo& client);
        void NotifyServerTick(uint64_t tick);
        /// What the editor tells its plugins about its ImGui (before NotifyEditorInit)
        void SetEditorImGui(const EditorImGui& imgui) { m_EditorImGui = imgui; }
        void NotifyEditorInit();
        void NotifyEditorShutdown();
        void NotifySelectionChanged(const std::vector<std::string>& actorPaths);
        /// Runs the checks plugins added to the asset validator ("tool.validator" items, and ToolPlugin::OnToolTask for "validate") : the problems, one line each
        std::vector<std::string> RunToolValidators(const std::string& projectFile);
        /// ToolPlugin : false if a plugin refused the task
        bool NotifyToolTask(const std::string& task, const std::string& project, std::string& message);

        /// Datapack patches: change a world's JSON the way the patches of the loaded modules say (installed as a world JSON filter)
        void PatchWorld(const std::string& worldName, nlohmann::json& world);

        // ---- Trust and registries ----
        const TrustStore& Trust() const { return m_Trust; }
        TrustStore& Trust() { return m_Trust; }
        /// Where `plugin.install` puts plugins ("" : none)
        const std::string& InstallDirectory() const { return m_InstallDirectory; }
        /// Adds a publisher to the trust store and saves it in the first trust file
        Core::Result<bool> TrustPublisher(const std::string& publisher, const std::string& keyHex);
        /// Installs a plugin and its dependencies from the configured registries. They load on the next start.
        Core::Result<std::vector<InstalledPlugin>> InstallPlugin(const std::string& id, const std::string& range = "*");

        // ---- Lockfile ----
        nlohmann::json LockfileJson() const;
        /// What differs between `lock` and the plugins that are loaded, one line each
        std::vector<std::string> VerifyLockfile(const nlohmann::json& lock) const;
        bool WriteLockfile(const std::string& path) const;
        const std::vector<std::string>& LockfileProblems() const { return m_LockProblems; }

        // ---- Reports ----
        std::string Describe() const;
        std::string DescribePlugin(const std::string& id) const;
        nlohmann::json DescribeJson() const;
        /// Per-plugin cost, from the scheduler's statistics ; warns once for each plugin that goes over its budget
        void UpdateBudgets();

        // ---- Safe mode ----
        /// The plugin running when the process last crashed, if the crash happened inside plugin code
        static std::string ReadSafeModeSuspect(const std::string& stateDir);
        /// The plugin whose code runs now (owner id, 0 : none) : what a crash is blamed on
        uint32_t ActivePlugin() const { return m_ActivePlugin.load(std::memory_order_relaxed); }

        /// For tests : forget everything. Plugins are shut down first.
        void ResetForTests();

    private:
        PluginManager() = default;
        ~PluginManager() = default;

        struct StaticEntry {
            std::string manifestJson;
            PluginFactory factory;
        };

        // discovery
        void DiscoverFolder(const std::string& folder, std::vector<PluginManifest>& out, std::vector<std::string>& problems, int depth);
        bool DiscoverPluginDir(const std::string& dir, std::vector<PluginManifest>& out, std::vector<std::string>& problems);
        bool ImplicitManifestForLibrary(const std::string& library, const std::string& id, PluginManifest& out);
        void ReadProject(std::vector<std::string>& extraFolders, std::vector<std::string>& extraPlugins, ResolveOptions& options, std::map<std::string, nlohmann::json>& settings);

        std::string ExtractArchive(const std::string& archive, std::vector<std::string>& problems);
        void ApplyTrustPolicy(std::vector<PluginManifest>& candidates, std::set<std::string>& allowUnsigned);
        Core::Result<std::vector<Registry>> LoadRegistries();

        // loading
        bool LoadRecord(PluginRecord& record);
        /// An editor plugin that is enabled or disabled while the editor runs gets OnEditorInit / OnEditorShutdown like the ones present at its start
        void EditorInitOne(PluginRecord& record);
        void EditorShutdownOne(PluginRecord& record);
        bool LoadLibrary(PluginRecord& record);
        bool LoadSandbox(PluginRecord& record);       ///< abi "wasm1" : wasm/wasm_plugin.hpp
        bool CreateContext(PluginRecord& record);
        void RunLoad(PluginRecord& record);
        void RunInitialize(PluginRecord& record);
        void MountContent(PluginRecord& record);
        void Fail(PluginRecord& record, const std::string& message);
        void FailDependents(const std::string& id, const std::string& because);
        bool Guard(PluginRecord& record, const char* what, const std::function<void()>& call);
        void UnloadRecord(PluginRecord& record, bool keepLibrary);
        void ApplySettings(const PluginRecord& record, const std::map<std::string, nlohmann::json>& projectSettings);
        void ComputeHashes(PluginRecord& record);
        HostBindings MakeBindings();
        void RegisterBuiltinPlugins();
        void RegisterConsole();
        void UnregisterConsole();
        void InstallWorldBridge();
        void RemoveWorldBridge();
        void RunSafeModeCheck(PluginManagerConfig& config, std::set<std::string>& disabled);
        void WriteSafeModeMarker(const std::string& suspect);
        void ClearSafeModeMarker();
        std::vector<PluginRecord*> Dependents(const std::string& id, bool onlyEnabled);

        PluginManagerConfig m_Config;
        Callbacks m_Callbacks;
        bool m_Started = false;
        bool m_EngineReady = false;
        bool m_WorldsAside = false;      // Reload has set the worlds aside : their components are not alive
        bool m_ShuttingDown = false;
        bool m_Dirty = false;

        std::vector<StaticEntry> m_Static;
        std::vector<std::unique_ptr<PluginRecord>> m_Records;
        std::vector<Rejection> m_Rejections;
        std::vector<std::string> m_LockProblems;
        std::map<std::string, nlohmann::json> m_ProjectSettings;
        std::set<std::string> m_Features;
        std::vector<DeclaredComponentEntry> m_Declared;
        TrustStore m_Trust;
        std::map<std::string, SignatureInfo> m_Signatures;
        std::set<std::string> m_InstalledIds;
        std::string m_InstallDirectory;
        EditorImGui m_EditorImGui;
        bool m_EditorInitialized = false;          ///< NotifyEditorInit ran and NotifyEditorShutdown has not
        std::set<std::string> m_ProjectAllowUnsigned;
        std::vector<std::string> m_ProjectRegistries;

        HookRegistry m_Hooks;
        ServiceRegistry m_Services;
        ExtensionRegistry m_Extensions;
        ContentMounts m_Content;

        Core::IEngineContext* m_Engine = nullptr;
        Core::Ecs::Scheduler* m_Scheduler = nullptr;
        Events::EventDispatcher* m_Events = nullptr;

        std::atomic<uint32_t> m_ActivePlugin{ 0 };
        std::vector<std::pair<PluginOwner, std::string>> m_PendingErrors;     // from systems, handled at the start of a frame
        std::mutex m_ErrorMutex;
        uint64_t m_FrameCounter = 0;
        uint32_t m_PatchFilterId = 0;
        std::vector<std::string> m_ConsoleCommands;
        std::map<std::string, uint64_t> m_EventWatches;     // event.watch : name -> its subscription
        bool m_BridgeInstalled = false;
        uint32_t m_TempCounter = 0;
        std::vector<std::string> m_TempFiles;
    };
}
