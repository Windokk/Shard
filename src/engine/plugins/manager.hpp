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

    struct PluginRecord {
        PluginManifest manifest;
        PluginState state = PluginState::Discovered;
        std::string error;
        std::vector<std::string> warnings;
        PluginOwner owner = 0;
        bool required = false;
        bool isStatic = false;
        bool legacy = false;

        IPlugin* instance = nullptr;
        EnginePlugin* enginePlugin = nullptr;
        EditorPlugin* editorPlugin = nullptr;
        ServerPlugin* serverPlugin = nullptr;
        ToolPlugin* toolPlugin = nullptr;
        IModule* module = nullptr;
        const PluginDesc* desc = nullptr;
        PluginFactory staticFactory = nullptr;
        Core::Platform::DynLib library;
        std::string loadedFrom;
        std::unique_ptr<PluginContext> context;

        SignatureInfo signature;
        bool installed = false;
        std::string sourceArchive;

        uint64_t binaryHash = 0;
        uint64_t contentHash = 0;
        uint64_t identityHash = 0;

        double frameMsAverage = 0.0;
        double systemsMsAverage = 0.0;
        uint32_t errorCount = 0;
        bool overBudget = false;
        double memoryMB = 0.0;
        bool overMemoryBudget = false;
        uint32_t reloadCount = 0;

        bool IsRunning() const { return state == PluginState::Enabled; }
        bool IsLive() const { return state == PluginState::Initialized || state == PluginState::Enabled || state == PluginState::Disabled; }
    };

    struct PluginSetEntry {
        std::string id;
        std::string version;
        uint64_t hash = 0;
    };

    struct PluginSetDiff {
        std::vector<std::string> missing;
        std::vector<std::string> extra;
        std::vector<std::string> different;
        bool Same() const { return missing.empty() && extra.empty() && different.empty(); }
        std::string Describe() const;
    };

    PluginSetDiff ComparePluginSets(const std::vector<PluginSetEntry>& local, const std::vector<PluginSetEntry>& remote);
    nlohmann::json PluginSetToJson(const std::vector<PluginSetEntry>& set);
    std::vector<PluginSetEntry> PluginSetFromJson(const nlohmann::json& json);
    uint64_t HashPluginSet(const std::vector<PluginSetEntry>& set);
    std::vector<std::string> PluginSetToLines(const std::vector<PluginSetEntry>& set);
    std::vector<PluginSetEntry> PluginSetFromLines(const std::vector<std::string>& lines);

    struct DeclaredComponentEntry {
        std::string pluginId;
        DeclaredComponent component;
    };

    struct PluginManagerConfig {
        Host host = Host::Player;
        std::vector<std::string> searchPaths;
        std::vector<std::string> pluginPaths;
        std::string gameLibrary;
        std::string projectFile;
        SemVer engineVersion;
        std::set<std::string> extraFeatures;
        std::set<std::string> disabled;
        bool deterministic = false;
        bool readOnly = false;
        bool strictLock = false;
        bool allowHotReload = false;
        bool ignoreSafeMode = false;
        std::string lockfilePath;
        std::string stateDirectory;
        bool registerConsoleCommands = true;

        std::vector<std::string> trustFiles;
        std::string cacheDirectory;
        std::vector<std::string> registries;
        std::string installDirectory;
        bool allowUnsigned = false;
    };

    // Startup: discover > resolve > load > OnLoad > OnInitialize > mount content. OnEngineSystemsReady adds waiting systems and subscriptions. Shutdown: OnDisable > OnUnload > registrations removed > library unloaded. One instance per process, main thread only.
    class PluginManager {
    public:
        static PluginManager& Get();

        void AddStaticPlugin(const std::string& manifestJson, PluginFactory factory);

        struct Callbacks {
            // Worlds hold components with code in the library, so they are set aside around a reload. Without these a reload is refused while a world is loaded.
            std::function<bool(std::string& error)> suspendWorlds;
            std::function<void()> resumeWorlds;
            std::function<bool()> hasLoadedWorlds;
        };
        void SetCallbacks(Callbacks callbacks) { m_Callbacks = std::move(callbacks); }

        bool Startup(const PluginManagerConfig& config, std::string* error = nullptr);
        bool IsStarted() const { return m_Started; }

        void OnEngineSystemsReady(Core::IEngineContext* engine, Core::Ecs::Scheduler* scheduler, Events::EventDispatcher* events);
        void OnEngineInitialized();
        void BeginFrame(uint64_t frame);
        void EndFrame(uint64_t frame);
        bool BeforePlay();
        void AfterPlay();
        bool BeforeWorldLoad(const std::string& worldName);
        void AfterWorldLoad(const std::string& worldName);
        bool BeforeSave(const std::string& what);
        // Systems and subscriptions point into objects about to be deleted, so plugins give everything back first
        void OnEngineDestroying();
        void Shutdown();

        bool Enable(const std::string& id, std::string* error = nullptr);
        // With `cascade` dependents stop first, otherwise refused if any exist. Refused while a world is loaded if the plugin registered components, since worlds hold instances. Unload worlds or use Reload.
        bool Disable(const std::string& id, bool cascade = true, std::string* error = nullptr);
        bool Reload(const std::string& id, std::string* error = nullptr);

        const std::vector<std::unique_ptr<PluginRecord>>& Plugins() const { return m_Records; }
        PluginRecord* Find(const std::string& id);
        const std::vector<Rejection>& Rejections() const { return m_Rejections; }
        const PluginManagerConfig& Config() const { return m_Config; }

        HookRegistry& Hooks() { return m_Hooks; }
        ServiceRegistry& Services() { return m_Services; }
        ExtensionRegistry& Extensions() { return m_Extensions; }
        ContentMounts& Content() { return m_Content; }
        const std::vector<DeclaredComponentEntry>& DeclaredComponents() const { return m_Declared; }

        const std::set<std::string>& EngineFeatures() const { return m_Features; }

        std::vector<PluginSetEntry> SimulationPluginSet() const;
        uint64_t SimulationPluginHash() const { return HashPluginSet(SimulationPluginSet()); }

        void CheckClientPlugins(const nlohmann::json& clientSet, ConnectDecision& decision) const;
        void NotifyServerStart(bool dedicated);
        void NotifyServerStop(bool dedicated);
        void NotifyClientConnect(ClientInfo& client, ConnectDecision& decision);
        void NotifyClientDisconnect(const ClientInfo& client);
        void NotifyServerTick(uint64_t tick);
        void SetEditorImGui(const EditorImGui& imgui) { m_EditorImGui = imgui; }
        void NotifyEditorInit();
        void NotifyEditorShutdown();
        void NotifySelectionChanged(const std::vector<std::string>& actorPaths);
        std::vector<std::string> RunToolValidators(const std::string& projectFile);
        bool NotifyToolTask(const std::string& task, const std::string& project, std::string& message);

        void PatchWorld(const std::string& worldName, nlohmann::json& world);

        const TrustStore& Trust() const { return m_Trust; }
        TrustStore& Trust() { return m_Trust; }
        const std::string& InstallDirectory() const { return m_InstallDirectory; }
        Core::Result<bool> TrustPublisher(const std::string& publisher, const std::string& keyHex);
        Core::Result<std::vector<InstalledPlugin>> InstallPlugin(const std::string& id, const std::string& range = "*");

        nlohmann::json LockfileJson() const;
        std::vector<std::string> VerifyLockfile(const nlohmann::json& lock) const;
        bool WriteLockfile(const std::string& path) const;
        const std::vector<std::string>& LockfileProblems() const { return m_LockProblems; }

        std::string Describe() const;
        std::string DescribePlugin(const std::string& id) const;
        nlohmann::json DescribeJson() const;
        void UpdateBudgets();

        static std::string ReadSafeModeSuspect(const std::string& stateDir);
        uint32_t ActivePlugin() const { return m_ActivePlugin.load(std::memory_order_relaxed); }

        void ResetForTests();

    private:
        PluginManager() = default;
        ~PluginManager() = default;

        struct StaticEntry {
            std::string manifestJson;
            PluginFactory factory;
        };

        void DiscoverFolder(const std::string& folder, std::vector<PluginManifest>& out, std::vector<std::string>& problems, int depth);
        bool DiscoverPluginDir(const std::string& dir, std::vector<PluginManifest>& out, std::vector<std::string>& problems);
        bool ImplicitManifestForLibrary(const std::string& library, const std::string& id, PluginManifest& out);
        void ReadProject(std::vector<std::string>& extraFolders, std::vector<std::string>& extraPlugins, ResolveOptions& options, std::map<std::string, nlohmann::json>& settings);

        std::string ExtractArchive(const std::string& archive, std::vector<std::string>& problems);
        void ApplyTrustPolicy(std::vector<PluginManifest>& candidates, std::set<std::string>& allowUnsigned);
        Core::Result<std::vector<Registry>> LoadRegistries();

        bool LoadRecord(PluginRecord& record);
        void EditorInitOne(PluginRecord& record);
        void EditorShutdownOne(PluginRecord& record);
        bool LoadLibrary(PluginRecord& record);
        bool LoadSandbox(PluginRecord& record);
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
        bool m_WorldsAside = false;
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
        bool m_EditorInitialized = false;
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
        std::vector<std::pair<PluginOwner, std::string>> m_PendingErrors;
        std::mutex m_ErrorMutex;
        uint64_t m_FrameCounter = 0;
        uint32_t m_PatchFilterId = 0;
        std::vector<std::string> m_ConsoleCommands;
        std::map<std::string, uint64_t> m_EventWatches;
        bool m_BridgeInstalled = false;
        uint32_t m_TempCounter = 0;
        std::vector<std::string> m_TempFiles;
    };
}
