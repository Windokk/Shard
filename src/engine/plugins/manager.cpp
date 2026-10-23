#include "engine/plugins/manager.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <new>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "config.h"
#include "engine/core/config/cvar.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/core/math/hash.hpp"
#include "engine/core/tags/tag_registry.hpp"
#include "engine/platform/crash/crash_handler.hpp"
#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/plugins/wasm/wasm_plugin.hpp"
#include "engine/plugins/archive.hpp"
#include "engine/plugins/declared_component.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_extension.hpp"

namespace Shard::Engine::Plugins {

    namespace fs = std::filesystem;
    using nlohmann::json;

    const char* PluginStateName(PluginState state)
    {
        switch (state) {
            case PluginState::Discovered: return "Discovered";
            case PluginState::Resolved: return "Resolved";
            case PluginState::Loaded: return "Loaded";
            case PluginState::Initialized: return "Initialized";
            case PluginState::Enabled: return "Enabled";
            case PluginState::Disabled: return "Disabled";
            case PluginState::Failed: return "Failed";
            case PluginState::Unloaded: return "Unloaded";
        }
        return "?";
    }

    namespace {
        struct StaticPending {
            std::string manifest;
            PluginFactory factory;
        };

        std::vector<StaticPending>& PendingStatic()
        {
            static std::vector<StaticPending> pending;
            return pending;
        }

        std::string Slash(std::string s)
        {
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        }

        std::string Abs(const std::string& path)
        {
            std::error_code ec;
            return Slash(fs::absolute(path, ec).lexically_normal().generic_string());
        }

        bool ReadWholeFile(const std::string& path, std::string& out)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            std::stringstream ss;
            ss << in.rdbuf();
            out = ss.str();
            return true;
        }

        uint64_t HashFileBytes(const std::string& path, uint64_t seed = 14695981039346656037ull)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) return seed;
            std::vector<char> buffer(1 << 16);
            uint64_t h = seed;
            while (in) {
                in.read(buffer.data(), std::streamsize(buffer.size()));
                const std::streamsize got = in.gcount();
                if (got > 0) h = Core::Fnv1a64(buffer.data(), size_t(got), h);
            }
            return h;
        }

        uint64_t HashDirectory(const std::string& root)
        {
            std::error_code ec;
            std::vector<std::string> files;
            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
                if (ec) break;
                if (it->is_regular_file(ec)) files.push_back(Slash(fs::relative(it->path(), root, ec).generic_string()));
            }
            std::sort(files.begin(), files.end());
            uint64_t h = 14695981039346656037ull;
            for (const std::string& f : files) {
                h = Core::Fnv1a64(f, h);
                h = HashFileBytes(root + "/" + f, h);
            }
            return h;
        }

        std::string JsonValueToString(const json& v)
        {
            if (v.is_string()) return v.get<std::string>();
            if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
            return v.dump();
        }

        // Stands in for the plugins the engine itself is made of
        class BuiltinModule final : public IModule {
        public:
            ModuleKind Kind() const override { return ModuleKind::Engine; }
        };

        // Datapacks have no code : this is what the manager loads for them
        class DatapackModule final : public IModule {
        public:
            ModuleKind Kind() const override { return ModuleKind::Datapack; }
        };

        // A library of the old kind : InitializeSingletons(engine, components, logger) then RegisterGameComponents()
        class LegacyModule final : public IModule {
        public:
            using InitFn = void (*)(Core::IEngineContext*, Objects::Components::ComponentRegistry*, Debugging::Logger*);
            using RegisterFn = void (*)();
            LegacyModule(InitFn init, RegisterFn reg) : m_Init(init), m_Register(reg) {}
            ModuleKind Kind() const override { return ModuleKind::Code; }
            void OnLoad(PluginContext& ctx) override
            {
                const std::vector<std::string> before = ctx.Components().Snapshot();
                if (m_Register) m_Register();
                ctx.Components().AdoptNewSince(before);
            }
            void Init(const HostLinks& links) { if (m_Init) m_Init(links.engine, links.components, links.logger); }
        private:
            InitFn m_Init;
            RegisterFn m_Register;
        };

        // What a world tells its extensions, forwarded to the modules
        class PluginWorldBridge final : public Worlds::IWorldExtension {
        public:
            void OnLoad(Worlds::World& world) override { Forward(world, [&](IModule* m) { m->OnWorldLoaded(world); }, "OnWorldLoaded"); }
            void OnPlay(Worlds::World& world) override { Forward(world, [&](IModule* m) { m->OnPlayStart(world); }, "OnPlayStart"); }
            void OnStop(Worlds::World& world) override { Forward(world, [&](IModule* m) { m->OnPlayStop(world); }, "OnPlayStop"); }
            void OnUnload(Worlds::World& world) override { Forward(world, [&](IModule* m) { m->OnWorldUnloaded(world); }, "OnWorldUnloaded"); }
        private:
            template <typename F>
            void Forward(Worlds::World&, F&& call, const char* what);
        };

        PluginManager* gBridgeOwner = nullptr;

        std::atomic<bool> gCrashHookInstalled{ false };
    }

    void RegisterStaticPlugin(const char* manifestJson, PluginFactory factory)
    {
        PendingStatic().push_back({ manifestJson, factory });
    }

    PluginManager& PluginManager::Get()
    {
        static PluginManager manager;
        return manager;
    }

    void PluginManager::AddStaticPlugin(const std::string& manifestJson, PluginFactory factory)
    {
        if (m_Started) {
            DEBUG_WARNING("Plugins : static plugin added after startup, ignored");
            return;
        }
        m_Static.push_back({ manifestJson, factory });
    }

    PluginRecord* PluginManager::Find(const std::string& id)
    {
        for (auto& r : m_Records) if (r->manifest.id == id) return r.get();
        return nullptr;
    }

    HostBindings PluginManager::MakeBindings()
    {
        HostBindings b;
        b.engine = m_Engine;
        b.scheduler = m_Scheduler;
        b.events = m_Events;
        b.components = &Objects::Components::GetComponentRegistry();
        b.cvars = &Core::CVarRegistry::Global();
        b.services = &m_Services;
        b.hooks = &m_Hooks;
        b.extensions = &m_Extensions;
        b.content = &m_Content;
        b.logger = &Debugging::GetLogger();
        b.activePlugin = &m_ActivePlugin;
        b.reportError = [this](PluginOwner owner, const std::string& message) {
            std::lock_guard<std::mutex> lock(m_ErrorMutex);
            m_PendingErrors.emplace_back(owner, message);
        };
        b.registerWorldExtension = [](std::type_index type, Worlds::WorldExtensionFactory factory) {
            for (const auto& entry : Worlds::GetWorldExtensionFactories())
                if (entry.first == type) return false;
            Worlds::RegisterWorldExtension(type, std::move(factory));
            return true;
        };
        b.unregisterWorldExtension = [](std::type_index type) { return Worlds::UnregisterWorldExtension(type); };
        b.registerSettingsRefs = [](Worlds::WorldSettingsAssetRefs refs) { return Worlds::RegisterWorldSettingsAssetRefsWithId(std::move(refs)); };
        b.unregisterSettingsRefs = [](uint32_t id) { return Worlds::UnregisterWorldSettingsAssetRefs(id); };
        b.allocate = [](size_t size, size_t align) -> void* { return ::operator new(size, std::align_val_t(align), std::nothrow); };
        b.deallocate = [](void* p, size_t, size_t align) { ::operator delete(p, std::align_val_t(align)); };
        return b;
    }

    // ---- Discovery ----

    bool PluginManager::DiscoverPluginDir(const std::string& dir, std::vector<PluginManifest>& out, std::vector<std::string>& problems)
    {
        const std::string manifestPath = dir + "/shard-plugin.json";
        std::error_code ec;
        if (!fs::is_regular_file(manifestPath, ec)) return false;
        std::string text;
        if (!ReadWholeFile(manifestPath, text)) {
            problems.push_back(manifestPath + " : cannot be read");
            return true;
        }
        auto parsed = ParseManifestText(text);
        if (!parsed) {
            problems.push_back(manifestPath + " : " + parsed.GetError().message);
            m_Rejections.push_back({ fs::path(dir).filename().string(), "", Rejection::Kind::Incompatible, "invalid manifest : " + parsed.GetError().message });
            return true;
        }
        PluginManifest m = std::move(parsed.Value());
        m.rootDir = Abs(dir);
        m.manifestPath = Abs(manifestPath);
        out.push_back(std::move(m));
        return true;
    }

    void PluginManager::DiscoverFolder(const std::string& folder, std::vector<PluginManifest>& out, std::vector<std::string>& problems, int depth)
    {
        std::error_code ec;
        if (!fs::is_directory(folder, ec)) return;
        if (DiscoverPluginDir(folder, out, problems)) return;
        if (depth >= 1) return;
        std::vector<std::string> subs;
        std::vector<std::string> archives;
        for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (it->is_directory(ec)) subs.push_back(Slash(it->path().generic_string()));
            else if (it->is_regular_file(ec) && it->path().extension() == kArchiveExtension) archives.push_back(Slash(it->path().generic_string()));
        }
        std::sort(archives.begin(), archives.end());
        for (const std::string& archive : archives) {
            const std::string unpacked = ExtractArchive(archive, problems);
            if (!unpacked.empty()) {
                const size_t before = out.size();
                DiscoverPluginDir(unpacked, out, problems);
                for (size_t i = before; i < out.size(); ++i) out[i].manifestPath = Slash(archive);      // where it came from, for the log
            }
        }
        std::sort(subs.begin(), subs.end());        // the order the file system lists them in must not matter
        for (const std::string& sub : subs) DiscoverFolder(sub, out, problems, depth + 1);
    }

    bool PluginManager::ImplicitManifestForLibrary(const std::string& library, const std::string& id, PluginManifest& out)
    {
        std::error_code ec;
        if (!fs::is_regular_file(library, ec)) return false;
        out = PluginManifest();
        out.id = id;
        out.name = id;
        out.version = SemVer{ 0, 0, 0, {} };
        out.kind = PluginKind::Module;
        out.moduleKind = ModuleKind::Code;
        out.linkage = Linkage::Dynamic;
        out.tier = TrustTier::FirstParty;
        out.hosts = kAllSimulationHosts | HostBit(Host::Tool);   // a library handed to a tool (--game) is meant to be loaded by it
        out.affectsSimulation = true;
        out.contentMount.clear();
        const fs::path abs = fs::absolute(library, ec).lexically_normal();
        out.rootDir = Slash(abs.parent_path().generic_string());
        out.binaries[CurrentPlatformKey()] = abs.filename().generic_string();
        out.manifestPath = "";
        return true;
    }

    void PluginManager::ReadProject(std::vector<std::string>& extraFolders, std::vector<std::string>& extraPlugins, ResolveOptions& options,
                                    std::map<std::string, json>& settings)
    {
        if (m_Config.projectFile.empty()) return;
        std::string text;
        if (!ReadWholeFile(m_Config.projectFile, text)) return;
        json project = json::parse(text, nullptr, false);
        if (project.is_discarded() || !project.is_object()) return;
        const fs::path root = fs::path(Abs(m_Config.projectFile)).parent_path();

        if (project.contains("pluginsFolder") && project["pluginsFolder"].is_string() && !project["pluginsFolder"].get<std::string>().empty())
            extraFolders.push_back(Slash((root / project["pluginsFolder"].get<std::string>()).lexically_normal().generic_string()));

        if (project.contains("registries") && project["registries"].is_array())
            for (const json& r : project["registries"]) if (r.is_string()) m_ProjectRegistries.push_back(r.get<std::string>());

        if (!project.contains("plugins") || !project["plugins"].is_array()) return;
        for (const json& entry : project["plugins"]) {
            std::string id;
            if (entry.is_string()) id = entry.get<std::string>();
            else if (entry.is_object() && entry.contains("id") && entry["id"].is_string()) id = entry["id"].get<std::string>();
            else { DEBUG_WARNING("Plugins : an entry of the project's plugin list has no id, ignored"); continue; }
            if (!entry.is_object()) continue;

            if (entry.value("enabled", true) == false) options.disabled.insert(id);
            if (entry.value("required", false)) options.required.insert(id);
            if (entry.value("allowUnsigned", false)) m_ProjectAllowUnsigned.insert(id);
            if (entry.contains("version") && entry["version"].is_string()) {
                std::string err;
                if (auto range = VersionRange::Parse(entry["version"].get<std::string>(), &err)) options.projectRanges.emplace_back(id, *range);
                else DEBUG_WARNING("Plugins : " + id + " : " + err);
            }
            if (entry.contains("path") && entry["path"].is_string())
                extraPlugins.push_back(Slash((root / entry["path"].get<std::string>()).lexically_normal().generic_string()));
            if (entry.contains("settings") && entry["settings"].is_object()) settings[id] = entry["settings"];
        }
    }

    std::string PluginManager::ExtractArchive(const std::string& archive, std::vector<std::string>& problems)
    {
        std::error_code ec;
        const std::string hash = Crypto::Sha256FileHex(archive);
        if (hash.empty()) { problems.push_back(archive + " : cannot be read"); return {}; }
        const fs::path folder = fs::path(m_Config.cacheDirectory) / hash.substr(0, 24);
        if (fs::exists(folder / kExtractedMarkerFile, ec)) return Slash(folder.generic_string());

        auto zip = ZipArchive::Open(archive);
        if (!zip) {
            problems.push_back(archive + " : " + zip.GetError().message);
            m_Rejections.push_back({ fs::path(archive).stem().string(), "", Rejection::Kind::Incompatible, "invalid archive : " + zip.GetError().message });
            return {};
        }
        const fs::path staging = fs::path(m_Config.cacheDirectory) / (hash.substr(0, 24) + ".partial");
        fs::remove_all(staging, ec);
        auto extracted = zip.Value().ExtractAll(staging.string());
        if (!extracted) {
            fs::remove_all(staging, ec);
            problems.push_back(archive + " : " + extracted.GetError().message);
            m_Rejections.push_back({ fs::path(archive).stem().string(), "", Rejection::Kind::Incompatible, "invalid archive : " + extracted.GetError().message });
            return {};
        }
        { std::ofstream marker(staging / kExtractedMarkerFile); marker << hash << "\n"; }
        fs::remove_all(folder, ec);
        fs::rename(staging, folder, ec);
        if (ec) { problems.push_back(archive + " : cannot unpack it into " + folder.string()); return {}; }
        return Slash(folder.generic_string());
    }

    void PluginManager::ApplyTrustPolicy(std::vector<PluginManifest>& candidates, std::set<std::string>& allowUnsigned)
    {
        std::vector<PluginManifest> kept;
        for (PluginManifest& m : candidates) {
            if (m.manifestPath.empty() || m.rootDir.empty()) { kept.push_back(std::move(m)); continue; }       // static, or a library given on its own

            std::error_code ec;
            InstallMarker marker;
            const bool installed = ReadInstallMarker(m.rootDir, marker) && marker.id == m.id;
            const bool hasSignature = fs::exists(fs::path(m.rootDir) / kSignatureFile, ec);
            SignatureInfo sig;
            if (hasSignature || installed || m.tier == TrustTier::Native) sig = VerifyPackage(m.rootDir, m_Trust);

            std::string refusal;
            if (sig.status == SignatureInfo::Status::Invalid) refusal = "its signature is invalid : " + sig.message;
            else if (sig.status == SignatureInfo::Status::Revoked) refusal = sig.message;
            else if (installed && sig.status != SignatureInfo::Status::Valid)
                refusal = "it was installed from a registry and is " + std::string(SignatureStatusName(sig.status)) + (sig.message.empty() ? "" : " (" + sig.message + ")");
            else if (m.tier == TrustTier::Native && sig.status != SignatureInfo::Status::Valid && !m_Config.allowUnsigned && !allowUnsigned.count(m.id))
                refusal = "a native plugin (tier 1) has to be signed by a trusted publisher (" + std::string(SignatureStatusName(sig.status)) +
                          "). Trust the publisher in plugin-trust.json, or set \"allowUnsigned\": true on its entry of the project's plugin list";

            if (!refusal.empty()) {
                m_Rejections.push_back({ m.id, m.version.ToString(), Rejection::Kind::Untrusted, refusal });
                continue;
            }
            if (m.tier == TrustTier::Native && sig.status != SignatureInfo::Status::Valid)
                DEBUG_WARNING("Plugins : " + m.id + " is a native plugin with no trusted signature, loaded because the project allows it");
            m_Signatures[m.id] = sig;
            if (installed) m_InstalledIds.insert(m.id);
            kept.push_back(std::move(m));
        }
        candidates = std::move(kept);
    }

    Core::Result<std::vector<Registry>> PluginManager::LoadRegistries()
    {
        std::vector<Registry> registries;
        std::string errors;
        for (const std::string& source : m_Config.registries) {
            Registry r;
            auto loaded = r.Load(source);
            if (!loaded) { errors += (errors.empty() ? "" : "\n") + source + " : " + loaded.GetError().message; continue; }
            registries.push_back(std::move(r));
        }
        if (registries.empty()) return Core::Err(errors.empty() ? std::string("no registry is configured (the project's \"registries\" list, SHARD_PLUGIN_REGISTRY)") : errors);
        return Core::Ok(std::move(registries));
    }

    Core::Result<bool> PluginManager::TrustPublisher(const std::string& publisher, const std::string& keyHex)
    {
        std::vector<uint8_t> bytes;
        if (!Crypto::FromHex(keyHex, bytes, 32)) return Core::Err("a key is 64 hexadecimal digits");
        if (publisher.empty()) return Core::Err("a publisher needs a name");
        Crypto::PublicKey key;
        std::copy(bytes.begin(), bytes.end(), key.begin());
        m_Trust.Trust(publisher, key);
        if (m_Config.trustFiles.empty()) return Core::Ok(true);          // trusted for this run only
        return m_Trust.SaveFile(m_Config.trustFiles.front());
    }

    Core::Result<std::vector<InstalledPlugin>> PluginManager::InstallPlugin(const std::string& id, const std::string& rangeText)
    {
        if (m_InstallDirectory.empty()) return Core::Err("there is no plugins folder to install into (open a project)");
        std::string err;
        auto range = VersionRange::Parse(rangeText, &err);
        if (!range) return Core::Err(err);
        auto registries = LoadRegistries();
        if (!registries) return Core::Err(registries.GetError().message);
        InstallOptions options;
        options.installDirectory = m_InstallDirectory;
        options.engineVersion = m_Config.engineVersion;
        options.trust = &m_Trust;
        options.allowUnsigned = m_Config.allowUnsigned;
        for (const auto& r : m_Records) if (r->isStatic) options.alreadyAvailable.push_back(r->manifest.id);
        PluginInstaller installer(std::move(registries.Value()));
        return installer.Install(id, *range, options);
    }

    // ---- Startup ----

    void PluginManager::RegisterBuiltinPlugins()
    {
        // The parts of the engine, as modules : a plugin can depend on them, and `plugin.list` shows what the engine is made of
        struct Builtin { const char* id; const char* name; };
        static const Builtin kBuiltins[] = {
            { "shard.core", "Core and platform" }, { "shard.assets", "Assets" }, { "shard.world", "World and ECS" },
            { "shard.physics", "Physics" }, { "shard.audio", "Audio" }, { "shard.renderer", "Renderer" },
        };
        for (const Builtin& b : kBuiltins) {
            json m;
            m["schema"] = 1;
            m["id"] = b.id;
            m["name"] = b.name;
            m["version"] = m_Config.engineVersion.ToString();
            m["kind"] = "module";
            m["moduleKind"] = "engine";
            m["linkage"] = "static";
            m["hosts"] = json::array({ "engine", "tool" });
            m["simulation"] = { { "affects", false }, { "deterministic", true } };
            m_Static.push_back({ m.dump(), []() -> IPlugin* { return new BuiltinModule(); } });
        }
    }

    bool PluginManager::Startup(const PluginManagerConfig& configIn, std::string* error)
    {
        if (m_Started) return true;
        m_Config = configIn;
        m_Engine = Core::gSharedEnginePtr;
        m_Rejections.clear();
        m_LockProblems.clear();
        m_ProjectSettings.clear();

        if (m_Config.engineVersion == SemVer{}) {
            m_Config.engineVersion = SemVer{ uint32_t(VERSION_MAJOR), uint32_t(VERSION_MINOR), uint32_t(VERSION_PATCH), {} };
        }
        m_Features = m_Config.extraFeatures;
        m_Features.insert({ "physics", "audio", "renderer" });
#if defined(SHARD_DOUBLE_PRECISION_PHYSICS)
        m_Features.insert("largeWorld");
#endif
#if defined(SHARD_DETERMINISTIC_PHYSICS)
        m_Features.insert("determinism");
#endif
        if (m_Config.host == Host::Headless) m_Features.insert("headless");
        if (m_Config.host == Host::Editor) m_Features.insert("editor");

        if (m_Config.stateDirectory.empty() && !m_Config.projectFile.empty())
            m_Config.stateDirectory = Slash((fs::path(Abs(m_Config.projectFile)).parent_path() / ".cache").generic_string());
        if (m_Config.lockfilePath.empty() && !m_Config.projectFile.empty())
            m_Config.lockfilePath = Slash((fs::path(Abs(m_Config.projectFile)).parent_path() / "plugins.lock").generic_string());

        if (m_Config.cacheDirectory.empty())
            m_Config.cacheDirectory = !m_Config.stateDirectory.empty() ? m_Config.stateDirectory + "/plugin_cache"
                                                                      : Slash((fs::temp_directory_path() / "shard_plugin_cache").generic_string());
        if (m_Config.trustFiles.empty() && !m_Config.projectFile.empty())
            m_Config.trustFiles.push_back(Slash((fs::path(Abs(m_Config.projectFile)).parent_path() / "plugin-trust.json").generic_string()));
        m_Trust = TrustStore();
        for (const std::string& file : m_Config.trustFiles) {
            std::error_code tec;
            if (!fs::exists(file, tec)) continue;
            auto loaded = m_Trust.LoadFile(file);
            if (!loaded) DEBUG_WARNING("Plugins : trust file " + file + " : " + loaded.GetError().message);
        }
        m_Signatures.clear();
        m_InstalledIds.clear();
        m_ProjectAllowUnsigned.clear();
        m_ProjectRegistries.clear();

        // Static plugins: the engine's modules, then the ones the application compiled in
        RegisterBuiltinPlugins();
        for (auto& p : PendingStatic()) m_Static.push_back({ p.manifest, p.factory });
        PendingStatic().clear();

        // Safe mode: a plugin the last run crashed in sits this one out
        std::set<std::string> disabled = m_Config.disabled;
        RunSafeModeCheck(m_Config, disabled);

        // What to look at
        std::vector<PluginManifest> candidates;
        std::vector<std::string> problems;
        std::vector<std::string> projectFolders, projectPlugins;
        ResolveOptions options;
        options.host = m_Config.host;
        options.engineVersion = m_Config.engineVersion;
        options.engineFeatures = m_Features;
        options.disabled = disabled;
        ReadProject(projectFolders, projectPlugins, options, m_ProjectSettings);
        m_InstallDirectory = !m_Config.installDirectory.empty() ? m_Config.installDirectory : (projectFolders.empty() ? std::string() : projectFolders.front());
        for (const std::string& r : m_ProjectRegistries) m_Config.registries.push_back(r);

        for (const StaticEntry& s : m_Static) {
            auto parsed = ParseManifestText(s.manifestJson);
            if (!parsed) { problems.push_back("static plugin : " + parsed.GetError().message); continue; }
            PluginManifest m = std::move(parsed.Value());
            m.linkage = Linkage::Static;
            candidates.push_back(std::move(m));
        }
        const size_t staticCount = candidates.size();

        for (const std::string& folder : m_Config.searchPaths) DiscoverFolder(Abs(folder), candidates, problems, 0);
        for (const std::string& folder : projectFolders) DiscoverFolder(folder, candidates, problems, 0);
        auto discoverArchive = [&](const std::string& archive) {
            const std::string unpacked = ExtractArchive(archive, problems);
            if (!unpacked.empty()) DiscoverPluginDir(unpacked, candidates, problems);
        };
        for (const std::string& dir : projectPlugins) {
            if (fs::path(dir).extension() == kArchiveExtension) discoverArchive(Abs(dir));
            else DiscoverFolder(dir, candidates, problems, 1);
        }
        for (const std::string& path : m_Config.pluginPaths) {
            std::error_code ec;
            if (fs::path(path).extension() == kArchiveExtension) { discoverArchive(Abs(path)); continue; }
            if (fs::is_directory(path, ec)) { DiscoverFolder(Abs(path), candidates, problems, 0); continue; }
            if (fs::path(path).filename() == "shard-plugin.json") { DiscoverPluginDir(Abs(fs::path(path).parent_path().string()), candidates, problems); continue; }
            // A library on its own : "game" is what the old --game option called the game's library
            PluginManifest implicit;
            std::string id = fs::path(path).stem().string();
            for (char& c : id) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (id.compare(0, 3, "lib") == 0) id = id.substr(3);
            for (char& c : id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) c = '-';
            if (ImplicitManifestForLibrary(Abs(path), id, implicit)) candidates.push_back(std::move(implicit));
            else problems.push_back("plugin path '" + path + "' is not a plugin folder, a manifest or a library");
        }
        if (!m_Config.gameLibrary.empty()) {
            PluginManifest game;
            if (ImplicitManifestForLibrary(Abs(m_Config.gameLibrary), "game", game)) candidates.push_back(std::move(game));
            else problems.push_back("game library '" + m_Config.gameLibrary + "' does not exist");
        }
        for (const std::string& p : problems) DEBUG_WARNING("Plugins : " + p);

        // Who made what : a bad signature, or a plugin that needs a good one and has none, is left out here
        ApplyTrustPolicy(candidates, m_ProjectAllowUnsigned);

        // Resolve
        ResolveResult resolved = ResolvePlugins(candidates, options);
        for (const Rejection& r : resolved.rejected) {
            m_Rejections.push_back(r);
            if (r.kind == Rejection::Kind::WrongHost || r.kind == Rejection::Kind::Duplicate || r.kind == Rejection::Kind::Disabled)
                DEBUG_INFO("Plugins : " + r.id + " " + r.version + " not loaded : " + r.reason);
            else
                DEBUG_WARNING("Plugins : " + r.id + " " + r.version + " not loaded : " + r.reason);
        }
        std::string startupError;
        for (const std::string& e : resolved.errors) startupError += (startupError.empty() ? "" : "\n") + e;

        // The records, in load order
        PluginOwner nextOwner = 0;
        for (size_t index : resolved.order) {
            auto record = std::make_unique<PluginRecord>();
            record->manifest = std::move(candidates[index]);
            record->state = PluginState::Resolved;
            record->owner = ++nextOwner;
            record->isStatic = index < staticCount;
            record->required = options.required.count(record->manifest.id) != 0;
            if (auto sig = m_Signatures.find(record->manifest.id); sig != m_Signatures.end()) record->signature = sig->second;
            record->installed = m_InstalledIds.count(record->manifest.id) != 0;
            if (record->isStatic) {
                for (const StaticEntry& s : m_Static) {
                    auto parsed = ParseManifestText(s.manifestJson);
                    if (parsed && parsed.Value().id == record->manifest.id) { record->staticFactory = s.factory; break; }
                }
            }
            m_Records.push_back(std::move(record));
        }

        // A plugin the project requires has to be there at all
        for (const std::string& id : options.required) {
            if (!Find(id)) {
                bool explained = false;
                for (const std::string& e : resolved.errors) if (e.find("'" + id + "'") != std::string::npos) explained = true;
                if (!explained) startupError += (startupError.empty() ? "" : "\n") + std::string("required plugin '") + id + "' was not found";
            }
        }

        if (m_Config.registerConsoleCommands) RegisterConsole();

        if (!gCrashHookInstalled.exchange(true)) {
            Core::Platform::CrashHandler::AddHook([](const Core::Platform::CrashInfo&) {
                PluginManager& pm = PluginManager::Get();
                const uint32_t active = pm.m_ActivePlugin.load();
                if (active == 0 || pm.m_Config.stateDirectory.empty()) return;
                for (auto& r : pm.m_Records) if (r->owner == active) { pm.WriteSafeModeMarker(r->manifest.id); break; }
            });
        }

        // Settings first (the variables a plugin creates in OnLoad then start with the right value)
        for (auto& r : m_Records) ApplySettings(*r, m_ProjectSettings);

        // Load, register
        for (auto& r : m_Records) {
            if (r->state == PluginState::Failed) continue;
            ComputeHashes(*r);
            if (!LoadRecord(*r)) continue;
        }
        for (auto& r : m_Records) if (r->state == PluginState::Loaded) RunLoad(*r);
        for (auto& r : m_Records) if (r->state == PluginState::Loaded) RunInitialize(*r);
        for (auto& r : m_Records) if (r->state == PluginState::Initialized) MountContent(*r);

        for (const ContentConflict& c : m_Content.FindConflicts())
            DEBUG_WARNING("Plugins : '" + c.logicalPath + "' is provided by " + c.keptFrom + " and " + c.rejectedFrom + ", which does not list it in content.overrides : " + c.keptFrom + "'s is used");

        // A required plugin that did not make it stops the start
        for (auto& r : m_Records)
            if (r->required && r->state == PluginState::Failed)
                startupError += (startupError.empty() ? "" : "\n") + std::string("required plugin '") + r->manifest.id + "' failed : " + r->error;

        // plugins.lock
        if (!m_Config.lockfilePath.empty()) {
            std::string lockText;
            if (ReadWholeFile(m_Config.lockfilePath, lockText)) {
                json lock = json::parse(lockText, nullptr, false);
                if (lock.is_discarded()) m_LockProblems.push_back("plugins.lock is not valid JSON");
                else m_LockProblems = VerifyLockfile(lock);
                for (const std::string& line : m_LockProblems) DEBUG_WARNING("Plugins : plugins.lock : " + line);
                if (m_Config.strictLock && !m_LockProblems.empty()) {
                    startupError += (startupError.empty() ? "" : "\n") + std::string("plugins.lock does not match the plugins that were loaded :");
                    for (const std::string& line : m_LockProblems) startupError += "\n  " + line;
                }
            } else if (!m_Config.readOnly) {
                if (WriteLockfile(m_Config.lockfilePath)) DEBUG_INFO("Plugins : wrote " + m_Config.lockfilePath);
            }
        }

        ClearSafeModeMarker();
        m_Started = true;
        m_Dirty = false;

        size_t live = 0;
        for (auto& r : m_Records) if (r->IsLive()) ++live;
        DEBUG_INFO("Plugins : " + std::to_string(live) + " of " + std::to_string(m_Records.size()) + " loaded (" + std::string(HostName(m_Config.host)) + " host)");

        if (!startupError.empty()) {
            if (error) *error = startupError;
            return false;
        }
        return true;
    }

    void PluginManager::ApplySettings(const PluginRecord& record, const std::map<std::string, json>& projectSettings)
    {
        Core::CVarRegistry& cvars = Core::CVarRegistry::Global();
        const std::string prefix = record.manifest.id + ".";
        auto apply = [&](Core::ConfigLayer layer, const std::string& key, const json& value) {
            const std::string name = key.compare(0, prefix.size(), prefix) == 0 ? key : prefix + key;
            std::string err;
            if (!cvars.SetLayerValue(layer, name, JsonValueToString(value), &err))
                DEBUG_WARNING("Plugins : " + record.manifest.id + " : setting '" + key + "' : " + err);
        };
        if (record.manifest.settings.is_object())
            for (auto it = record.manifest.settings.begin(); it != record.manifest.settings.end(); ++it) apply(Core::ConfigLayer::Engine, it.key(), it.value());
        auto found = projectSettings.find(record.manifest.id);
        if (found != projectSettings.end())
            for (auto it = found->second.begin(); it != found->second.end(); ++it) apply(Core::ConfigLayer::Project, it.key(), it.value());
    }

    void PluginManager::ComputeHashes(PluginRecord& r)
    {
        r.binaryHash = 0;
        if (!r.isStatic && r.manifest.moduleKind != ModuleKind::Datapack) {
            const std::string bin = r.manifest.BinaryForThisPlatform();
            if (!bin.empty()) {
                const std::string full = fs::path(bin).is_absolute() ? bin : r.manifest.rootDir + "/" + bin;
                r.binaryHash = HashFileBytes(full);
            }
        }
        r.contentHash = 0;
        if (r.manifest.HasContent() && r.manifest.affectsSimulation && !r.isStatic) {
            std::error_code ec;
            const std::string root = r.manifest.rootDir + "/" + r.manifest.contentMount;
            if (fs::is_directory(root, ec)) r.contentHash = HashDirectory(root);
        }
        uint64_t h = Core::Fnv1a64(r.manifest.id);
        h = Core::Fnv1a64(r.manifest.version.ToString(), h);
        // The code is identified by the version : the same source gives other machine code on each platform, and a replay or a server
        // has to match across platforms. The content (data files) is byte for byte the same everywhere.
        h = Core::HashCombine(h, r.contentHash);
        r.identityHash = h;
    }

    bool PluginManager::LoadLibrary(PluginRecord& r)
    {
        std::string bin = r.manifest.BinaryForThisPlatform();
        const std::string source = fs::path(bin).is_absolute() ? bin : r.manifest.rootDir + "/" + bin;
        std::error_code ec;
        if (!fs::is_regular_file(source, ec)) { Fail(r, "binary '" + source + "' does not exist"); return false; }

        std::string path = source;
        if (m_Config.allowHotReload && !m_Config.deterministic) {
            // Windows keeps a loaded DLL locked : the copy is what gets loaded, the original stays free to be rebuilt
            static const fs::path tempRoot = fs::temp_directory_path() / ("shard_plugins_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(tempRoot, ec);
            const fs::path copy = tempRoot / (r.manifest.id + "-" + std::to_string(++m_TempCounter) + Core::Platform::DynLib::Extension());
            fs::copy_file(source, copy, fs::copy_options::overwrite_existing, ec);
            if (!ec) { path = Slash(copy.generic_string()); m_TempFiles.push_back(path); }
        }

        m_ActivePlugin = r.owner;
        WriteSafeModeMarker(r.manifest.id);
        const bool opened = r.library.Open(path);
        m_ActivePlugin = 0;
        if (!opened) { Fail(r, "cannot load '" + path + "' : " + r.library.LastError()); return false; }
        r.loadedFrom = path;

        HostLinks links;
        links.engine = m_Engine;
        links.components = &Objects::Components::GetComponentRegistry();
        links.logger = &Debugging::GetLogger();
        links.tags = &Core::TagRegistry::Get();

        // A library handed over on its own (--game, a bare path) has no manifest to say which interface it speaks : one built with the SDK
        // exports the C entry point and not the C++ descriptor
        if (r.manifest.abi == "cpp" && r.manifest.manifestPath.empty() && !r.library.Symbol<DescribeFn>(r.manifest.entry) &&
            r.library.Symbol<ShardPluginEntryFn>("shard_plugin_entry")) {
            r.manifest.abi = "c1";
            r.manifest.entry = "shard_plugin_entry";
        }

        if (r.manifest.abi == "c1") {
            // The C ABI : no engine types cross, so the build fingerprint does not apply (the ABI version is the contract)
            auto entry = r.library.Symbol<ShardPluginEntryFn>(r.manifest.entry);
            if (!entry) { Fail(r, "'" + path + "' does not export the C ABI entry point '" + r.manifest.entry + "'"); return false; }
            ShardPluginInfo info;
            std::memset(&info, 0, sizeof(info));
            ShardResult result = SHARD_OK;
            if (!Guard(r, "entry", [&] { result = entry(GetCHostApi(), &info); })) return false;
            if (result != SHARD_OK) { Fail(r, "its entry point '" + r.manifest.entry + "' refused to load (" + std::to_string(result) + ")"); return false; }
            const std::string problem = ValidateCPluginInfo(info);
            if (!problem.empty()) { Fail(r, problem); return false; }
            r.instance = CreateCPlugin(r.owner, info);
            return true;
        }

        auto describe = r.library.Symbol<DescribeFn>(r.manifest.entry);
        if (!describe) {
            // The old kind of game library
            auto init = r.library.Symbol<LegacyModule::InitFn>("InitializeSingletons");
            auto reg = r.library.Symbol<LegacyModule::RegisterFn>("RegisterGameComponents");
            if (!init || !reg) { Fail(r, "'" + path + "' exports neither " + r.manifest.entry + " nor the legacy InitializeSingletons / RegisterGameComponents"); return false; }
            auto* legacy = new LegacyModule(init, reg);
            Guard(r, "InitializeSingletons", [&] { legacy->Init(links); });
            r.legacy = true;
            r.instance = legacy;
            r.module = legacy;
            DEBUG_WARNING("Plugins : " + r.manifest.id + " uses the old library interface (InitializeSingletons) ; export it with SHARD_PLUGIN to get the build check and the full API");
            return r.state != PluginState::Failed;
        }

        const PluginDesc* desc = describe();
        if (!desc || desc->structSize < sizeof(uint32_t) * 3 || !desc->create || !desc->destroy) { Fail(r, "its descriptor is malformed"); return false; }
        if (desc->apiMajor != kPluginApiMajor || desc->apiMinor > kPluginApiMinor) {
            Fail(r, "built for plugin API " + std::to_string(desc->apiMajor) + "." + std::to_string(desc->apiMinor) + ", this engine has " +
                    std::to_string(kPluginApiMajor) + "." + std::to_string(kPluginApiMinor));
            return false;
        }
        const std::string theirs = desc->fingerprint ? desc->fingerprint : "";
        if (theirs != kBuildFingerprint) {
            Fail(r, "build fingerprint mismatch : the plugin was built as '" + theirs + "', this executable is '" + kBuildFingerprint + "'");
            return false;
        }
        if (!r.manifest.abiFingerprint.empty() && r.manifest.abiFingerprint != kBuildFingerprint) {
            Fail(r, "build fingerprint mismatch : its manifest says it was built as '" + r.manifest.abiFingerprint + "', this executable is '" + kBuildFingerprint + "'");
            return false;
        }
        r.desc = desc;
        if (desc->link) {
            if (!Guard(r, "link", [&] { desc->link(&links); })) return false;
        }
        if (!Guard(r, "create", [&] { r.instance = desc->create(); })) return false;
        if (!r.instance) { Fail(r, "create() returned nothing"); return false; }
        return true;
    }

    bool PluginManager::LoadSandbox(PluginRecord& r)
    {
        const std::string bin = r.manifest.BinaryForThisPlatform();
        if (bin.empty()) { Fail(r, "its manifest gives no \"wasm\" binary"); return false; }
        const std::string source = fs::path(bin).is_absolute() ? bin : r.manifest.rootDir + "/" + bin;
        std::error_code ec;
        if (!fs::is_regular_file(source, ec)) { Fail(r, "binary '" + source + "' does not exist"); return false; }
        // Nothing is mapped into the process : no temporary copy, no build fingerprint, no safe mode marker (a trap cannot crash the host)
        m_ActivePlugin = r.owner;
        auto plugin = CreateWasmPlugin(r.owner, r.manifest, source);
        m_ActivePlugin = 0;
        if (!plugin) { Fail(r, "sandbox : " + plugin.GetError().message); return false; }
        r.loadedFrom = source;
        r.instance = plugin.Value();
        return true;
    }

    bool PluginManager::LoadRecord(PluginRecord& r)
    {
        if (r.isStatic) {
            if (!r.staticFactory) { Fail(r, "no factory"); return false; }
            if (!Guard(r, "create", [&] { r.instance = r.staticFactory(); })) return false;
        } else if (r.manifest.moduleKind == ModuleKind::Datapack) {
            r.instance = new DatapackModule();
        } else {
            if (r.manifest.abi == "wasm1") {
                if (!LoadSandbox(r)) return false;
            } else if (r.manifest.tier == TrustTier::Sandboxed) {
                Fail(r, "a sandboxed plugin (tier 2) must be a WebAssembly module (\"abi\": \"wasm1\")");
                return false;
            } else if (!LoadLibrary(r)) return false;
        }
        if (!r.instance) { Fail(r, "no instance"); return false; }

        r.enginePlugin = dynamic_cast<EnginePlugin*>(r.instance);
        r.editorPlugin = dynamic_cast<EditorPlugin*>(r.instance);
        r.serverPlugin = dynamic_cast<ServerPlugin*>(r.instance);
        r.toolPlugin = dynamic_cast<ToolPlugin*>(r.instance);
        if (!r.module) r.module = dynamic_cast<IModule*>(r.instance);
        if (r.manifest.abi == "c1" || r.manifest.abi == "wasm1") {
            // A C plugin (a sandboxed one is a C plugin to the host) is one object whatever callbacks it fills : the kinds it also is are objects of their own
            const CPluginFacets facets = GetCPluginFacets(r.instance);
            r.enginePlugin = facets.engine;
            r.toolPlugin = facets.tool;
        }
        if (r.manifest.IsModule() && !r.module && !r.legacy)
            DEBUG_WARNING("Plugins : " + r.manifest.id + " says it is a module but its class does not derive from IModule");

        r.state = PluginState::Loaded;
        return true;
    }

    bool PluginManager::CreateContext(PluginRecord& r)
    {
        r.context = std::make_unique<PluginContext>(r.manifest, r.owner, m_Config.host, MakeBindings());
        if (m_Scheduler || m_Events) r.context->BindEngine(m_Scheduler, m_Events, m_Engine);
        return true;
    }

    bool PluginManager::Guard(PluginRecord& r, const char* what, const std::function<void()>& call)
    {
        m_ActivePlugin = r.owner;
        std::string failure;
        try {
            call();
        } catch (const std::exception& e) {
            failure = std::string(what) + " threw : " + e.what();
        } catch (...) {
            failure = std::string(what) + " threw";
        }
        m_ActivePlugin = 0;
        if (failure.empty()) return true;
        ++r.errorCount;
        Fail(r, failure);
        return false;
    }

    void PluginManager::Fail(PluginRecord& r, const std::string& message)
    {
        if (r.state == PluginState::Failed) return;
        r.state = PluginState::Failed;
        r.error = message;
        DEBUG_ERROR("Plugins : " + r.manifest.id + " failed : " + message);
        if (r.context) {
            try { r.context->ReleaseAll(); } catch (...) {}
        }
        m_Content.Unmount(r.owner);
        Core::TagRegistry::Get().RemoveDeclarations(Core::TagSource::Plugin, r.manifest.id);
        FailDependents(r.manifest.id, r.manifest.id);
    }

    void PluginManager::FailDependents(const std::string& id, const std::string& because)
    {
        std::set<std::string> provided;
        if (PluginRecord* self = Find(id))
            for (const ProvidedService& p : self->manifest.provides) provided.insert("service:" + p.service);
        for (auto& r : m_Records) {
            if (r->state == PluginState::Failed || r->state == PluginState::Unloaded) continue;
            bool depends = false;
            for (const DependencySpec& d : r->manifest.dependencies)
                if (!d.optional && (d.id == id || provided.count(d.id))) depends = true;
            if (depends) Fail(*r, "dependency '" + because + "' failed");
        }
    }

    void PluginManager::RunLoad(PluginRecord& r)
    {
        CreateContext(r);
        WriteSafeModeMarker(r.manifest.id);
        Guard(r, "OnLoad", [&] { r.instance->OnLoad(*r.context); });
    }

    void PluginManager::RunInitialize(PluginRecord& r)
    {
        WriteSafeModeMarker(r.manifest.id);
        if (!Guard(r, "OnInitialize", [&] { r.instance->OnInitialize(*r.context); })) return;
        r.state = PluginState::Initialized;
    }

    void PluginManager::MountContent(PluginRecord& r)
    {
        // The tags of the manifest join the tree with the plugin's content (and leave it with it, see UnmountContent's callers)
        if (!r.manifest.tags.empty()) {
            std::vector<std::pair<std::string, std::string>> tags(r.manifest.tags.begin(), r.manifest.tags.end());
            Core::TagRegistry::Get().SetDeclarations(Core::TagSource::Plugin, r.manifest.id, tags);
        }

        if (!r.manifest.HasContent()) return;
        std::string err;
        if (!m_Content.Mount(r.manifest, r.owner, &err)) {
            if (r.manifest.moduleKind == ModuleKind::Datapack) Fail(r, err);
            else DEBUG_WARNING("Plugins : " + r.manifest.id + " : " + err);
            return;
        }
        if (!m_Content.Find(r.manifest.id)) return;       // nothing to mount

        const std::string root = r.manifest.rootDir + "/" + r.manifest.contentMount;
        const std::vector<std::string> problems = ValidateContentFolder(root);
        if (!problems.empty()) {
            std::string all;
            for (const std::string& p : problems) all += "\n  " + p;
            if (r.manifest.moduleKind == ModuleKind::Datapack) { Fail(r, "its content is not valid :" + all); return; }
            r.warnings.push_back("content problems :" + all);
            DEBUG_WARNING("Plugins : " + r.manifest.id + " content problems :" + all);
        }

        // Components declared by the pack
        for (const std::string& file : m_Content.List(r.manifest.id, "components")) {
            if (file.size() < 5 || file.compare(file.size() - 5, 5, ".json") != 0) continue;
            auto text = m_Content.ReadText(ContentMounts::MakeModUri(r.manifest.id, file));
            if (!text) continue;
            json j = json::parse(*text, nullptr, false);
            if (j.is_discarded()) continue;
            auto declared = ParseDeclaredComponent(j);
            if (!declared) continue;
            // Registered as a real component type : it can be added to an actor, saved, loaded, copied and edited like one written in C++
            auto type = DeclaredComponentType::Build(declared.Value());
            if (!type) {
                r.warnings.push_back(type.GetError().message);
                DEBUG_WARNING("Plugins : " + r.manifest.id + " : " + type.GetError().message);
            } else if (r.context) {
                r.context->Components().Register(declared.Value().name, MakeDeclaredFactory(type.Value()));
            }
            m_Declared.push_back({ r.manifest.id, std::move(declared.Value()) });
        }

        if (r.module) Guard(r, "OnContentMounted", [&] { r.module->OnContentMounted(*r.context); });
    }

    // ---- Engine lifecycle ----

    template <typename F>
    void PluginWorldBridge::Forward(Worlds::World&, F&& call, const char* what)
    {
        PluginManager& pm = PluginManager::Get();
        for (auto& r : pm.Plugins()) {
            if (!r->IsRunning() || !r->module) continue;
            PluginRecord* rec = r.get();
            // A module that throws is counted, the others still hear about the world
            try { call(rec->module); } catch (const std::exception& e) {
                DEBUG_ERROR("Plugins : " + rec->manifest.id + " " + std::string(what) + " threw : " + e.what());
                ++rec->errorCount;
            } catch (...) {
                DEBUG_ERROR("Plugins : " + rec->manifest.id + " " + std::string(what) + " threw");
                ++rec->errorCount;
            }
        }
    }

    void PluginManager::InstallWorldBridge()
    {
        if (m_BridgeInstalled) return;
        Worlds::RegisterWorldExtension<PluginWorldBridge>();
        gBridgeOwner = this;
        m_BridgeInstalled = true;
    }

    void PluginManager::RemoveWorldBridge()
    {
        if (!m_BridgeInstalled) return;
        Worlds::UnregisterWorldExtension(std::type_index(typeid(PluginWorldBridge)));
        m_BridgeInstalled = false;
    }

    void PluginManager::OnEngineSystemsReady(Core::IEngineContext* engine, Core::Ecs::Scheduler* scheduler, Events::EventDispatcher* events)
    {
        if (!m_Started) return;
        m_Engine = engine;
        m_Scheduler = scheduler;
        m_Events = events;
        // What C systems asked of the actors (shard.ecs set_local_transform) is done once the phase is over, on this thread
        if (scheduler) scheduler->SetPhaseEndHook([this](Core::Ecs::Phase) {
            try { FlushCPoses(m_Engine); } catch (const std::exception& e) { DEBUG_ERROR(std::string("Plugins : applying the poses of C systems threw : ") + e.what()); }
        });

        Hooks::PreEngineInit pre;
        try { m_Hooks.Emit(pre); } catch (const std::exception& e) { DEBUG_ERROR(std::string("Plugins : PreEngineInit handler threw : ") + e.what()); }

        InstallWorldBridge();
        Worlds::SetWorldSaveGuard([this](const std::string& name) { return BeforeSave(name); });
        if (m_PatchFilterId == 0) {
            m_PatchFilterId = Worlds::AddWorldJsonFilter([this](const std::string& name, json& world) { PatchWorld(name, world); });
        }
        for (auto& r : m_Records) {
            if (r->context) r->context->BindEngine(scheduler, events, engine);       // the contexts made before the engine existed learn it too
        }
    }

    void PluginManager::OnEngineInitialized()
    {
        if (!m_Started || m_EngineReady) return;
        for (auto& r : m_Records) {
            if (r->state != PluginState::Initialized) continue;
            WriteSafeModeMarker(r->manifest.id);
            if (!Guard(*r, "OnEnable", [&] { r->instance->OnEnable(*r->context); })) continue;
            r->state = PluginState::Enabled;
        }
        ClearSafeModeMarker();
        m_EngineReady = true;
        Hooks::PostEngineInit post;
        try { m_Hooks.Emit(post); } catch (const std::exception& e) { DEBUG_ERROR(std::string("Plugins : PostEngineInit handler threw : ") + e.what()); }
    }

    void PluginManager::OnEngineDestroying()
    {
        if (!m_Started) return;
        Hooks::PreShutdown pre;
        try { m_Hooks.Emit(pre); } catch (...) {}
        for (auto it = m_Records.rbegin(); it != m_Records.rend(); ++it) {
            PluginRecord& r = **it;
            if (r.state != PluginState::Enabled && r.state != PluginState::Initialized) continue;
            Guard(r, "OnDisable", [&] { r.instance->OnDisable(*r.context); });
            if (r.context) r.context->ReleaseAll();
            if (r.state != PluginState::Failed) r.state = PluginState::Disabled;
        }
        RemoveWorldBridge();
        m_EventWatches.clear();         // the dispatcher goes with the engine
        Worlds::SetWorldSaveGuard(nullptr);
        if (m_PatchFilterId != 0) { Worlds::RemoveWorldJsonFilter(m_PatchFilterId); m_PatchFilterId = 0; }
        if (m_Scheduler) m_Scheduler->SetPhaseEndHook(nullptr);
        m_Scheduler = nullptr;
        m_Events = nullptr;
        m_EngineReady = false;
    }

    void PluginManager::UnloadRecord(PluginRecord& r, bool keepLibrary)
    {
        if (r.state == PluginState::Enabled || r.state == PluginState::Initialized) {
            Guard(r, "OnDisable", [&] { r.instance->OnDisable(*r.context); });
        }
        if (r.instance && r.state != PluginState::Unloaded && r.state != PluginState::Discovered && r.state != PluginState::Resolved && r.context) {
            // OnUnload even for a plugin that failed : it may hold things only it can release. Its own exceptions are not escalated any more.
            try { m_ActivePlugin = r.owner; r.instance->OnUnload(*r.context); } catch (...) {}
            m_ActivePlugin = 0;
        }
        if (r.context) {
            r.context->ReleaseAll();
            if (r.context->Memory().Used() != 0)
                DEBUG_WARNING("Plugins : " + r.manifest.id + " still holds " + std::to_string(r.context->Memory().Used()) + " bytes of counted memory after OnUnload (a leak)");
            r.context.reset();
        }
        m_Content.Unmount(r.owner);
        Core::TagRegistry::Get().RemoveDeclarations(Core::TagSource::Plugin, r.manifest.id);
        m_Declared.erase(std::remove_if(m_Declared.begin(), m_Declared.end(), [&](const DeclaredComponentEntry& d) { return d.pluginId == r.manifest.id; }), m_Declared.end());
        m_Services.RevokeOwner(r.owner);
        m_Hooks.RemoveOwner(r.owner);
        m_Extensions.RemoveOwner(r.owner);

        if (r.instance) {
            if (r.desc && r.desc->destroy) r.desc->destroy(r.instance);
            else delete r.instance;
            r.instance = nullptr;
        }
        r.enginePlugin = nullptr; r.editorPlugin = nullptr; r.serverPlugin = nullptr; r.toolPlugin = nullptr; r.module = nullptr;
        r.desc = nullptr;
        if (!keepLibrary) {
            ReleaseWasmPlugin(r.owner);
            r.library.Close();
            r.loadedFrom.clear();
        }
        r.state = PluginState::Unloaded;
    }

    void PluginManager::Shutdown()
    {
        if (!m_Started) return;
        m_ShuttingDown = true;
        for (auto it = m_Records.rbegin(); it != m_Records.rend(); ++it) {
            PluginRecord& r = **it;
            if (r.state == PluginState::Unloaded) continue;
            UnloadRecord(r, false);
        }
        UnregisterConsole();
        std::error_code ec;
        for (const std::string& f : m_TempFiles) fs::remove(f, ec);
        m_TempFiles.clear();
        m_Records.clear();
        m_Static.clear();
        m_Started = false;
        m_EngineReady = false;
        m_ShuttingDown = false;
        m_Engine = nullptr;
    }

    void PluginManager::ResetForTests()
    {
        if (m_Started) {
            OnEngineDestroying();
            Shutdown();
        }
        m_Records.clear();
        m_Static.clear();
        m_Rejections.clear();
        m_Declared.clear();
        m_Content.Clear();
        m_Hooks = HookRegistry();
        m_Extensions = ExtensionRegistry();
        m_Callbacks = Callbacks();
        m_Engine = nullptr;
        if (m_Scheduler) m_Scheduler->SetPhaseEndHook(nullptr);
        m_Scheduler = nullptr;
        m_Events = nullptr;
        m_LockProblems.clear();
        m_PendingErrors.clear();
        PendingStatic().clear();
    }

    // ---- Frames ----

    void PluginManager::BeginFrame(uint64_t frame)
    {
        if (!m_Started || !m_EngineReady) return;
        m_FrameCounter = frame;
        BeginWasmFrame();

        // What the systems reported since last frame. A plugin that keeps throwing is switched off here, outside the scheduler.
        std::vector<std::pair<PluginOwner, std::string>> errors;
        {
            std::lock_guard<std::mutex> lock(m_ErrorMutex);
            errors.swap(m_PendingErrors);
        }
        for (auto& [owner, message] : errors) {
            for (auto& r : m_Records) {
                if (r->owner != owner) continue;
                ++r->errorCount;
                if (r->errorCount <= 3) DEBUG_ERROR("Plugins : " + r->manifest.id + " : a system threw : " + message);
                if (r->errorCount == 10) {
                    Fail(*r, "too many errors, the last one : " + message);
                }
            }
        }

        Hooks::PreFrame hook(frame);
        try { m_Hooks.Emit(hook); } catch (const std::exception& e) { DEBUG_ERROR(std::string("Plugins : PreFrame handler threw : ") + e.what()); }

        for (auto& r : m_Records) {
            if (!r->IsRunning() || !r->enginePlugin) continue;
            const auto start = std::chrono::steady_clock::now();
            Guard(*r, "OnFrameBegin", [&] { r->enginePlugin->OnFrameBegin(frame); });
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            r->frameMsAverage = r->frameMsAverage * 0.9 + ms * 0.1;
        }
    }

    void PluginManager::EndFrame(uint64_t frame)
    {
        if (!m_Started || !m_EngineReady) return;
        for (auto& r : m_Records) {
            if (!r->IsRunning() || !r->enginePlugin) continue;
            const auto start = std::chrono::steady_clock::now();
            Guard(*r, "OnFrameEnd", [&] { r->enginePlugin->OnFrameEnd(frame); });
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            r->frameMsAverage = r->frameMsAverage * 0.9 + ms * 0.1;
        }
        // What the C plugins' systems emitted during the frame
        try { FlushCEvents(m_Events); } catch (const std::exception& e) { DEBUG_ERROR(std::string("Plugins : a named event handler threw : ") + e.what()); }
        Hooks::PostFrame hook(frame);
        try { m_Hooks.Emit(hook); } catch (const std::exception& e) { DEBUG_ERROR(std::string("Plugins : PostFrame handler threw : ") + e.what()); }
        if (frame % 120 == 0) UpdateBudgets();
    }

    bool PluginManager::BeforePlay()
    {
        if (!m_Started) return true;
        Hooks::PrePlay hook;
        try { m_Hooks.Emit(hook); } catch (const std::exception& e) { DEBUG_ERROR(std::string("Plugins : PrePlay handler threw : ") + e.what()); }
        if (hook.cancelled) {
            DEBUG_WARNING("Plugins : play mode cancelled" + std::string(hook.reason.empty() ? "" : " : " + hook.reason));
            return false;
        }
        return true;
    }

    void PluginManager::AfterPlay()
    {
        if (!m_Started) return;
        Hooks::PostPlay hook;
        try { m_Hooks.Emit(hook); } catch (const std::exception& e) { DEBUG_ERROR(std::string("Plugins : PostPlay handler threw : ") + e.what()); }
    }

    bool PluginManager::BeforeWorldLoad(const std::string& worldName)
    {
        if (!m_Started) return true;
        Hooks::PreWorldLoad hook(worldName);
        try { m_Hooks.Emit(hook); } catch (const std::exception& e) { DEBUG_ERROR(std::string("Plugins : PreWorldLoad handler threw : ") + e.what()); }
        if (hook.cancelled) DEBUG_WARNING("Plugins : loading '" + worldName + "' cancelled" + std::string(hook.reason.empty() ? "" : " : " + hook.reason));
        return !hook.cancelled;
    }

    void PluginManager::AfterWorldLoad(const std::string& worldName)
    {
        if (!m_Started) return;
        Hooks::PostWorldLoad hook(worldName);
        try { m_Hooks.Emit(hook); } catch (const std::exception& e) { DEBUG_ERROR(std::string("Plugins : PostWorldLoad handler threw : ") + e.what()); }
    }

    bool PluginManager::BeforeSave(const std::string& what)
    {
        if (!m_Started) return true;
        Hooks::PreSave hook(what);
        try { m_Hooks.Emit(hook); } catch (const std::exception& e) { DEBUG_ERROR(std::string("Plugins : PreSave handler threw : ") + e.what()); }
        return !hook.cancelled;
    }

    void PluginManager::UpdateBudgets()
    {
        const auto stats = m_Scheduler ? m_Scheduler->Stats() : std::vector<Core::Ecs::Scheduler::SystemStats>();
        for (auto& r : m_Records) {
            if (!r->IsRunning()) continue;
            const std::string prefix = r->manifest.id + "/";
            double total = 0.0;
            for (const auto& s : stats) {
                if (s.enabled && s.name.compare(0, prefix.size(), prefix) == 0) total += s.averageMs;
            }
            r->systemsMsAverage = total;
            if (r->context) {
                r->memoryMB = double(r->context->Memory().Used()) / (1024.0 * 1024.0);
                if (r->manifest.budgetMemoryMB > 0.0) {
                    const bool over = r->memoryMB > r->manifest.budgetMemoryMB || r->context->Memory().WentOver();
                    if (over && !r->overMemoryBudget)
                        DEBUG_WARNING("Plugins : " + r->manifest.id + " holds " + std::to_string(r->memoryMB) + " MB of counted memory, its budget is " + std::to_string(r->manifest.budgetMemoryMB) + " MB" +
                                      (r->context->Memory().Failures() ? " (" + std::to_string(r->context->Memory().Failures()) + " allocations were refused)" : ""));
                    r->overMemoryBudget = over;
                }
            }
            const double budget = r->manifest.budgetFrameMs;
            if (budget > 0.0) {
                const bool over = total + r->frameMsAverage > budget;
                if (over && !r->overBudget) {
                    DEBUG_WARNING("Plugins : " + r->manifest.id + " uses " + std::to_string(total + r->frameMsAverage) + " ms per frame, its budget is " + std::to_string(budget) + " ms");
                }
                r->overBudget = over;
            }
        }
    }

    // ---- Safe mode ----

    std::string PluginManager::ReadSafeModeSuspect(const std::string& stateDir)
    {
        std::string text;
        if (stateDir.empty() || !ReadWholeFile(stateDir + "/plugin_safe_mode.json", text)) return {};
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return {};
        return j.value("suspect", std::string());
    }

    void PluginManager::WriteSafeModeMarker(const std::string& suspect)
    {
        if (m_Config.stateDirectory.empty() || m_Config.readOnly) return;
        std::error_code ec;
        fs::create_directories(m_Config.stateDirectory, ec);
        std::ofstream out(m_Config.stateDirectory + "/plugin_safe_mode.json", std::ios::trunc);
        if (out) out << json({ { "suspect", suspect } }).dump();
    }

    void PluginManager::ClearSafeModeMarker()
    {
        if (m_Config.stateDirectory.empty()) return;
        std::error_code ec;
        fs::remove(m_Config.stateDirectory + "/plugin_safe_mode.json", ec);
    }

    void PluginManager::RunSafeModeCheck(PluginManagerConfig& config, std::set<std::string>& disabled)
    {
        if (config.ignoreSafeMode || config.stateDirectory.empty()) return;
        const std::string suspect = ReadSafeModeSuspect(config.stateDirectory);
        if (suspect.empty()) return;
        DEBUG_WARNING("Plugins : the last run crashed while '" + suspect + "' was running or loading : it is left out of this run (--ignore-safe-mode loads it anyway)");
        disabled.insert(suspect);
        std::error_code ec;
        fs::remove(config.stateDirectory + "/plugin_safe_mode.json", ec);
    }
}
