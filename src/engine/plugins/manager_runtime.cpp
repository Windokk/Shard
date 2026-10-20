#include "engine/plugins/manager.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "engine/core/config/cvar.hpp"
#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Plugins {

    namespace fs = std::filesystem;
    using nlohmann::json;

    namespace {
        std::string Hex(uint64_t v)
        {
            char buf[17];
            std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
            return buf;
        }

        bool ParseHex(const std::string& s, uint64_t& out)
        {
            if (s.empty() || s.size() > 16) return false;
            char* end = nullptr;
            out = std::strtoull(s.c_str(), &end, 16);
            return end && *end == '\0';
        }

        bool DependsOn(const PluginRecord& r, const std::string& id, const std::set<std::string>& services)
        {
            for (const DependencySpec& d : r.manifest.dependencies)
                if (!d.optional && (d.id == id || services.count(d.id))) return true;
            return false;
        }
    }

    // ---- Plugin sets (replays, servers) ----

    uint64_t HashPluginSet(const std::vector<PluginSetEntry>& set)
    {
        std::vector<PluginSetEntry> sorted = set;
        std::sort(sorted.begin(), sorted.end(), [](const PluginSetEntry& a, const PluginSetEntry& b) { return a.id < b.id; });
        uint64_t h = 14695981039346656037ull;
        for (const PluginSetEntry& e : sorted) {
            h = Core::Fnv1a64(e.id, h);
            h = Core::Fnv1a64(e.version, h);
            h = Core::HashCombine(h, e.hash);
        }
        return h;
    }

    std::string PluginSetDiff::Describe() const
    {
        std::string out;
        auto add = [&](const char* title, const std::vector<std::string>& list) {
            if (list.empty()) return;
            out += std::string(out.empty() ? "" : "; ") + title + " : ";
            for (size_t i = 0; i < list.size(); ++i) out += (i ? ", " : "") + list[i];
        };
        add("missing here", missing);
        add("not in the other set", extra);
        add("different", different);
        return out.empty() ? "same plugins" : out;
    }

    PluginSetDiff ComparePluginSets(const std::vector<PluginSetEntry>& local, const std::vector<PluginSetEntry>& remote)
    {
        PluginSetDiff diff;
        for (const PluginSetEntry& r : remote) {
            auto it = std::find_if(local.begin(), local.end(), [&](const PluginSetEntry& l) { return l.id == r.id; });
            if (it == local.end()) diff.missing.push_back(r.id + " " + r.version);
            else if (it->version != r.version) diff.different.push_back(r.id + " (" + it->version + " here, " + r.version + " there)");
            else if (it->hash != r.hash) diff.different.push_back(r.id + " " + r.version + " (same version, other content)");
        }
        for (const PluginSetEntry& l : local) {
            if (std::none_of(remote.begin(), remote.end(), [&](const PluginSetEntry& r) { return r.id == l.id; }))
                diff.extra.push_back(l.id + " " + l.version);
        }
        std::sort(diff.missing.begin(), diff.missing.end());
        std::sort(diff.extra.begin(), diff.extra.end());
        std::sort(diff.different.begin(), diff.different.end());
        return diff;
    }

    json PluginSetToJson(const std::vector<PluginSetEntry>& set)
    {
        json arr = json::array();
        for (const PluginSetEntry& e : set) arr.push_back({ { "id", e.id }, { "version", e.version }, { "hash", Hex(e.hash) } });
        return arr;
    }

    std::vector<std::string> PluginSetToLines(const std::vector<PluginSetEntry>& set)
    {
        std::vector<std::string> lines;
        for (const PluginSetEntry& e : set) lines.push_back(e.id + " " + e.version + " " + Hex(e.hash));
        std::sort(lines.begin(), lines.end());
        return lines;
    }

    std::vector<PluginSetEntry> PluginSetFromLines(const std::vector<std::string>& lines)
    {
        std::vector<PluginSetEntry> out;
        for (const std::string& line : lines) {
            std::istringstream in(line);
            PluginSetEntry e;
            std::string hash;
            if (in >> e.id >> e.version >> hash) {
                ParseHex(hash, e.hash);
                out.push_back(std::move(e));
            }
        }
        return out;
    }

    std::vector<PluginSetEntry> PluginSetFromJson(const json& j)
    {
        std::vector<PluginSetEntry> out;
        if (!j.is_array()) return out;
        for (const json& e : j) {
            if (!e.is_object() || !e.contains("id") || !e["id"].is_string()) continue;
            PluginSetEntry entry;
            entry.id = e["id"].get<std::string>();
            entry.version = e.value("version", std::string());
            ParseHex(e.value("hash", std::string()), entry.hash);
            out.push_back(std::move(entry));
        }
        return out;
    }

    std::vector<PluginSetEntry> PluginManager::SimulationPluginSet() const
    {
        std::vector<PluginSetEntry> set;
        for (const auto& r : m_Records) {
            if (!r->IsLive() || r->state == PluginState::Disabled) continue;
            if (!r->manifest.affectsSimulation) continue;
            if (r->isStatic && r->manifest.id.compare(0, 6, "shard.") == 0) continue;     // the engine itself
            set.push_back({ r->manifest.id, r->manifest.version.ToString(), r->identityHash });
        }
        return set;
    }

    void PluginManager::CheckClientPlugins(const json& clientSet, ConnectDecision& decision) const
    {
        const PluginSetDiff diff = ComparePluginSets(SimulationPluginSet(), PluginSetFromJson(clientSet));
        if (!diff.Same()) decision.Refuse("plugins do not match the server's (" + diff.Describe() + ")");
    }

    // ---- Dependents ----

    std::vector<PluginRecord*> PluginManager::Dependents(const std::string& id, bool onlyEnabled)
    {
        std::set<std::string> services;
        if (PluginRecord* self = Find(id))
            for (const ProvidedService& p : self->manifest.provides) services.insert("service:" + p.service);
        std::vector<PluginRecord*> out;
        for (auto& r : m_Records) {
            if (r->manifest.id == id) continue;
            if (onlyEnabled ? !(r->state == PluginState::Enabled || r->state == PluginState::Initialized) : !r->IsLive()) continue;
            if (DependsOn(*r, id, services)) out.push_back(r.get());
        }
        return out;
    }

    // ---- Enable / disable / reload ----

    bool PluginManager::Disable(const std::string& id, bool cascade, std::string* error)
    {
        PluginRecord* r = Find(id);
        auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
        if (!r) return fail("no plugin '" + id + "'");
        if (r->state == PluginState::Disabled) return true;
        if (r->state != PluginState::Enabled && r->state != PluginState::Initialized) return fail("'" + id + "' is " + PluginStateName(r->state));
        if (r->isStatic && r->manifest.id.compare(0, 6, "shard.") == 0) return fail("'" + id + "' is a part of the engine");

        std::vector<PluginRecord*> dependents = Dependents(id, true);
        if (!dependents.empty() && !cascade) {
            std::string names;
            for (PluginRecord* d : dependents) names += (names.empty() ? "" : ", ") + d->manifest.id;
            return fail("'" + id + "' is needed by " + names);
        }
        // Components live in the worlds, whose instances run code of the library : nothing is stopped if one of them would have to go
        if (!m_WorldsAside && m_Callbacks.hasLoadedWorlds && m_Callbacks.hasLoadedWorlds()) {
            auto ownsComponents = [](const PluginRecord& p) { return p.context && p.context->Owns("component"); };
            if (ownsComponents(*r)) return fail("'" + id + "' adds components and a world is loaded : unload the worlds first");
            for (PluginRecord* d : dependents)
                if (ownsComponents(*d)) return fail("'" + d->manifest.id + "' (needs '" + id + "') adds components and a world is loaded : unload the worlds first");
        }

        // Those that need it stop first, the last loaded first
        std::sort(dependents.begin(), dependents.end(), [](PluginRecord* a, PluginRecord* b) { return a->owner > b->owner; });
        for (PluginRecord* d : dependents) {
            std::string e;
            if (!Disable(d->manifest.id, true, &e)) return fail(e);
        }

        if (r->state == PluginState::Enabled) Guard(*r, "OnDisable", [&] { r->instance->OnDisable(*r->context); });
        if (r->context) r->context->ReleaseAll();
        r->context.reset();
        m_Content.Unmount(r->owner);
        Core::TagRegistry::Get().RemoveDeclarations(Core::TagSource::Plugin, r->manifest.id);
        m_Declared.erase(std::remove_if(m_Declared.begin(), m_Declared.end(), [&](const DeclaredComponentEntry& d) { return d.pluginId == id; }), m_Declared.end());
        m_Services.RevokeOwner(r->owner);
        m_Hooks.RemoveOwner(r->owner);
        m_Extensions.RemoveOwner(r->owner);
        if (r->state != PluginState::Failed) r->state = PluginState::Disabled;
        DEBUG_INFO("Plugins : " + id + " disabled");
        return true;
    }

    bool PluginManager::Enable(const std::string& id, std::string* error)
    {
        PluginRecord* r = Find(id);
        auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
        if (!r) return fail("no plugin '" + id + "' (it was not found, or the project left it out : see plugin.list)");
        if (r->state == PluginState::Enabled) return true;

        // Whatever it needs runs first
        for (const DependencySpec& d : r->manifest.dependencies) {
            if (d.optional) continue;
            PluginRecord* dep = d.id.compare(0, 8, "service:") == 0 ? nullptr : Find(d.id);
            if (d.id.compare(0, 8, "service:") == 0) {
                for (auto& other : m_Records)
                    for (const ProvidedService& p : other->manifest.provides) if (p.service == d.id.substr(8)) dep = other.get();
            }
            if (!dep) return fail("it needs '" + d.id + "', which is not loaded");
            if (dep->state != PluginState::Enabled) {
                std::string e;
                if (!Enable(dep->manifest.id, &e)) return fail("it needs '" + dep->manifest.id + "' : " + e);
            }
        }

        if (r->state == PluginState::Failed || r->state == PluginState::Unloaded) {
            // Start it over from its files
            UnloadRecord(*r, false);
            r->error.clear();
            r->errorCount = 0;
            r->state = PluginState::Resolved;
            if (!LoadRecord(*r)) return fail(r->error);
        } else if (r->state != PluginState::Disabled) {
            return fail("'" + id + "' is " + PluginStateName(r->state));
        } else {
            r->state = PluginState::Loaded;
        }

        RunLoad(*r);
        if (r->state != PluginState::Loaded) return fail(r->error);
        RunInitialize(*r);
        if (r->state != PluginState::Initialized) return fail(r->error);
        MountContent(*r);
        if (r->state != PluginState::Initialized) return fail(r->error);
        if (m_EngineReady) {
            if (!Guard(*r, "OnEnable", [&] { r->instance->OnEnable(*r->context); })) return fail(r->error);
            r->state = PluginState::Enabled;
        }
        DEBUG_INFO("Plugins : " + id + " enabled");
        return true;
    }

    bool PluginManager::Reload(const std::string& id, std::string* error)
    {
        PluginRecord* r = Find(id);
        auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
        if (!r) return fail("no plugin '" + id + "'");
        if (!m_Config.allowHotReload) return fail("hot reload is off in this run");
        if (m_Config.deterministic) return fail("hot reload is off in a deterministic run");
        if (r->isStatic) return fail("'" + id + "' is compiled into the executable");
        if (r->manifest.moduleKind == ModuleKind::Datapack) return fail("a datapack has no library to reload (disable and enable it)");
        if (r->legacy) return fail("'" + id + "' uses the old library interface : export it with SHARD_PLUGIN to make it reloadable");
        if (!(r->state == PluginState::Enabled || r->state == PluginState::Initialized || r->state == PluginState::Disabled))
            return fail("'" + id + "' is " + PluginStateName(r->state));

        // What the plugin wants to keep
        StateWriter saved;
        if (r->state != PluginState::Disabled) {
            bool reloadable = false;
            Guard(*r, "OnPrepareReload", [&] { reloadable = r->instance->OnPrepareReload(saved); });
            if (r->state == PluginState::Failed) return fail(r->error);
            if (!reloadable) return fail("'" + id + "' does not support reloading (OnPrepareReload returned false)");
        }

        // The worlds hold components whose code lives in the library
        const bool worldsLoaded = m_Callbacks.hasLoadedWorlds && m_Callbacks.hasLoadedWorlds();
        bool suspended = false;
        if (worldsLoaded) {
            if (!m_Callbacks.suspendWorlds) return fail("a world is loaded and nothing can set it aside : unload it first");
            std::string e;
            if (!m_Callbacks.suspendWorlds(e)) return fail("could not set the worlds aside : " + e);
            suspended = true;
            m_WorldsAside = true;
        }

        // The plugins built on this one stop, and come back after it
        std::vector<PluginRecord*> dependents = Dependents(id, true);
        std::sort(dependents.begin(), dependents.end(), [](PluginRecord* a, PluginRecord* b) { return a->owner < b->owner; });
        std::vector<std::string> dependentIds;
        for (PluginRecord* d : dependents) dependentIds.push_back(d->manifest.id);
        std::string e;
        std::vector<PluginRecord*> reversed(dependents.rbegin(), dependents.rend());
        for (PluginRecord* d : reversed) Disable(d->manifest.id, true, &e);

        const bool wasEnabled = r->state != PluginState::Disabled;
        UnloadRecord(*r, false);
        r->error.clear();
        r->state = PluginState::Resolved;
        ComputeHashes(*r);
        bool ok = LoadRecord(*r);
        if (ok) {
            RunLoad(*r);
            if (r->state == PluginState::Loaded) RunInitialize(*r);
            if (r->state == PluginState::Initialized) MountContent(*r);
            if (r->state == PluginState::Initialized && wasEnabled && m_EngineReady) {
                if (Guard(*r, "OnEnable", [&] { r->instance->OnEnable(*r->context); })) r->state = PluginState::Enabled;
            }
            if (r->state == PluginState::Enabled || r->state == PluginState::Initialized) {
                json data = saved.Data();
                StateReader reader(data);
                Guard(*r, "OnRestore", [&] { r->instance->OnRestore(reader); });
            }
        }
        ok = r->state != PluginState::Failed && r->state != PluginState::Unloaded;
        if (ok) {
            ++r->reloadCount;
            for (const std::string& dep : dependentIds) Enable(dep, &e);
        }
        m_WorldsAside = false;
        if (suspended && m_Callbacks.resumeWorlds) m_Callbacks.resumeWorlds();
        if (!ok) return fail(r->error.empty() ? "reload failed" : r->error);
        DEBUG_INFO("Plugins : " + id + " reloaded");
        return true;
    }

    // ---- Server / editor / tool notifications ----

    void PluginManager::NotifyServerStart(bool dedicated)
    {
        for (auto& r : m_Records) {
            if (!r->IsRunning() || !r->serverPlugin) continue;
            ServerContext sc{ *r->context, dedicated };
            Guard(*r, "OnServerStart", [&] { r->serverPlugin->OnServerStart(sc); });
        }
    }

    void PluginManager::NotifyServerStop(bool dedicated)
    {
        for (auto it = m_Records.rbegin(); it != m_Records.rend(); ++it) {
            PluginRecord& r = **it;
            if (!r.IsRunning() || !r.serverPlugin) continue;
            ServerContext sc{ *r.context, dedicated };
            Guard(r, "OnServerStop", [&] { r.serverPlugin->OnServerStop(sc); });
        }
    }

    void PluginManager::NotifyClientConnect(ClientInfo& client, ConnectDecision& decision)
    {
        Hooks::PlayerConnecting hook(client.name, client.address);
        try { m_Hooks.Emit(hook); } catch (const std::exception& e) { DEBUG_ERROR(std::string("Plugins : PlayerConnecting handler threw : ") + e.what()); }
        if (hook.cancelled) decision.Refuse(hook.reason.empty() ? "refused by a plugin" : hook.reason);
        for (auto& r : m_Records) {
            if (!decision.allowed) break;
            if (!r->IsRunning() || !r->serverPlugin) continue;
            Guard(*r, "OnClientConnect", [&] { r->serverPlugin->OnClientConnect(client, decision); });
        }
    }

    void PluginManager::NotifyClientDisconnect(const ClientInfo& client)
    {
        for (auto& r : m_Records) {
            if (!r->IsRunning() || !r->serverPlugin) continue;
            Guard(*r, "OnClientDisconnect", [&] { r->serverPlugin->OnClientDisconnect(client); });
        }
    }

    void PluginManager::NotifyServerTick(uint64_t tick)
    {
        for (auto& r : m_Records) {
            if (!r->IsRunning() || !r->serverPlugin) continue;
            ServerContext sc{ *r->context, m_Config.host == Host::Headless };
            Guard(*r, "OnServerTick", [&] { r->serverPlugin->OnServerTick(sc, tick); });
        }
    }

    void PluginManager::NotifyEditorInit()
    {
        for (auto& r : m_Records) {
            if (!r->IsRunning() || !r->editorPlugin) continue;
            EditorContext ec{ *r->context, m_Extensions, m_EditorImGui };
            Guard(*r, "OnEditorInit", [&] { r->editorPlugin->OnEditorInit(ec); });
        }
    }

    void PluginManager::NotifyEditorShutdown()
    {
        for (auto it = m_Records.rbegin(); it != m_Records.rend(); ++it) {
            PluginRecord& r = **it;
            if (!r.IsRunning() || !r.editorPlugin) continue;
            EditorContext ec{ *r.context, m_Extensions, m_EditorImGui };
            Guard(r, "OnEditorShutdown", [&] { r.editorPlugin->OnEditorShutdown(ec); });
        }
    }

    void PluginManager::NotifySelectionChanged(const std::vector<std::string>& actorPaths)
    {
        for (auto& r : m_Records) {
            if (!r->IsRunning() || !r->editorPlugin) continue;
            Guard(*r, "OnSelectionChanged", [&] { r->editorPlugin->OnSelectionChanged(actorPaths); });
        }
    }

    bool PluginManager::NotifyToolTask(const std::string& task, const std::string& project, std::string& message)
    {
        for (auto& r : m_Records) {
            if (!r->IsRunning() || !r->toolPlugin) continue;
            bool ok = true;
            Guard(*r, "OnToolTask", [&] { ok = r->toolPlugin->OnToolTask(task, project, message); });
            if (!ok) return false;
        }
        return true;
    }

    std::vector<std::string> PluginManager::RunToolValidators(const std::string& projectFile)
    {
        std::vector<std::string> problems;
        for (const auto& item : m_Extensions.Get<ToolValidatorItem>("tool.validator")) {
            if (!item->validate) continue;
            try {
                for (std::string& line : item->validate(projectFile)) problems.push_back(item->name + " : " + std::move(line));
            } catch (const std::exception& e) {
                problems.push_back(item->name + " : the check threw : " + e.what());
            } catch (...) {
                problems.push_back(item->name + " : the check threw");
            }
        }
        std::string message;
        if (!NotifyToolTask("validate", projectFile, message))
            problems.push_back(message.empty() ? std::string("a plugin refused the validation") : message);
        return problems;
    }

    // ---- Datapack patches ----

    void PluginManager::PatchWorld(const std::string& worldName, json& world)
    {
        for (const ContentMounts::MountPoint& mount : m_Content.Mounts()) {
            for (const std::string& file : m_Content.List(mount.pluginId, "patches")) {
                if (file.size() < 6 || file.compare(file.size() - 6, 6, ".patch") != 0) continue;
                auto text = m_Content.ReadText(ContentMounts::MakeModUri(mount.pluginId, file));
                if (!text) continue;
                json patch = json::parse(*text, nullptr, false);
                if (patch.is_discarded() || !patch.is_object() || patch.value("world", std::string()) != worldName) continue;
                auto applied = ApplyWorldPatch(world, patch);
                if (!applied) DEBUG_WARNING("Plugins : " + mount.pluginId + " : " + file + " was not applied to " + worldName + " : " + applied.GetError().message);
                else DEBUG_INFO("Plugins : " + mount.pluginId + " patched " + worldName + " (" + file + ")");
            }
        }
    }

    // ---- Lockfile ----

    json PluginManager::LockfileJson() const
    {
        json lock;
        lock["schema"] = 1;
        lock["engine"] = m_Config.engineVersion.ToString();
        json plugins = json::array();
        int order = 0;
        for (const auto& r : m_Records) {
            if (!r->IsLive()) continue;
            if (r->isStatic && r->manifest.id.compare(0, 6, "shard.") == 0) continue;
            plugins.push_back({ { "id", r->manifest.id }, { "version", r->manifest.version.ToString() }, { "hash", Hex(r->identityHash) },
                                { "affectsSimulation", r->manifest.affectsSimulation }, { "order", order++ } });
        }
        lock["plugins"] = plugins;
        return lock;
    }

    std::vector<std::string> PluginManager::VerifyLockfile(const json& lock) const
    {
        std::vector<std::string> problems;
        if (!lock.is_object() || !lock.contains("plugins") || !lock["plugins"].is_array()) { problems.push_back("the file has no plugin list"); return problems; }
        std::set<std::string> locked;
        for (const json& e : lock["plugins"]) {
            if (!e.is_object() || !e.contains("id") || !e["id"].is_string()) continue;
            const std::string id = e["id"].get<std::string>();
            locked.insert(id);
            const PluginRecord* r = nullptr;
            for (const auto& rec : m_Records) if (rec->manifest.id == id) r = rec.get();
            if (!r || !r->IsLive()) { problems.push_back("'" + id + "' is locked but was not loaded"); continue; }
            const std::string version = e.value("version", std::string());
            if (version != r->manifest.version.ToString()) {
                problems.push_back("'" + id + "' is locked at " + version + ", loaded " + r->manifest.version.ToString());
                continue;
            }
            uint64_t hash = 0;
            if (ParseHex(e.value("hash", std::string()), hash) && hash != r->identityHash)
                problems.push_back("'" + id + "' " + version + " has other contents than when it was locked");
        }
        for (const auto& r : m_Records) {
            if (!r->IsLive()) continue;
            if (r->isStatic && r->manifest.id.compare(0, 6, "shard.") == 0) continue;
            if (!locked.count(r->manifest.id)) problems.push_back("'" + r->manifest.id + "' is loaded but not in the lock");
        }
        return problems;
    }

    bool PluginManager::WriteLockfile(const std::string& path) const
    {
        std::error_code ec;
        fs::create_directories(fs::path(path).parent_path(), ec);
        std::ofstream out(path, std::ios::trunc);
        if (!out) return false;
        out << LockfileJson().dump(2) << "\n";
        return bool(out);
    }

    // ---- Reports ----

    std::string PluginManager::Describe() const
    {
        std::ostringstream out;
        out << std::left << std::setw(4) << "#" << std::setw(34) << "id" << std::setw(12) << "version" << std::setw(10) << "kind"
            << std::setw(12) << "state" << std::setw(5) << "tier" << std::setw(10) << "ms/frame" << "notes\n";
        for (const auto& r : m_Records) {
            std::string kind = PluginKindName(r->manifest.kind);
            if (r->manifest.moduleKind != ModuleKind::None) kind = ModuleKindName(r->manifest.moduleKind);
            std::ostringstream ms;
            ms << std::fixed << std::setprecision(2) << (r->systemsMsAverage + r->frameMsAverage);
            std::string notes;
            if (r->isStatic) notes += "static ";
            if (r->legacy) notes += "legacy-abi ";
            if (r->manifest.affectsSimulation) notes += "sim ";
            if (r->overBudget || r->overMemoryBudget) notes += "OVER-BUDGET ";
            if (r->state == PluginState::Failed) notes += r->error;
            out << std::setw(4) << r->owner << std::setw(34) << r->manifest.id << std::setw(12) << r->manifest.version.ToString() << std::setw(10) << kind
                << std::setw(12) << PluginStateName(r->state) << std::setw(5) << int(r->manifest.tier) << std::setw(10) << ms.str() << notes << "\n";
        }
        for (const Rejection& rej : m_Rejections) {
            if (rej.kind == Rejection::Kind::WrongHost || rej.kind == Rejection::Kind::Duplicate) continue;
            out << "  -  " << std::setw(34) << rej.id << std::setw(12) << rej.version << "not loaded : " << rej.reason << "\n";
        }
        return out.str();
    }

    std::string PluginManager::DescribePlugin(const std::string& id) const
    {
        const PluginRecord* r = nullptr;
        for (const auto& rec : m_Records) if (rec->manifest.id == id) r = rec.get();
        if (!r) return "no plugin '" + id + "'";
        const PluginManifest& m = r->manifest;
        std::ostringstream out;
        out << m.name << " (" << m.id << " " << m.version.ToString() << ")\n";
        if (!m.description.empty()) out << m.description << "\n";
        out << "  " << PluginKindName(m.kind);
        if (m.moduleKind != ModuleKind::None) out << " / " << ModuleKindName(m.moduleKind);
        out << ", tier " << int(m.tier) << ", " << (m.linkage == Linkage::Static ? "static" : "dynamic") << ", state " << PluginStateName(r->state) << "\n";
        out << "  hosts :";
        for (Host h : { Host::Editor, Host::Player, Host::Headless, Host::Tool }) if (m.RunsOn(h)) out << " " << HostName(h);
        out << "\n  folder : " << m.rootDir << "\n";
        if (!m.dependencies.empty()) {
            out << "  needs :";
            for (const DependencySpec& d : m.dependencies) out << " " << d.id << " " << d.range.Text() << (d.optional ? " (optional)" : "") << ",";
            out << "\n";
        }
        if (!m.provides.empty()) {
            out << "  provides :";
            for (const ProvidedService& p : m.provides) out << " " << p.service << " " << p.version.ToString();
            out << "\n";
        }
        if (!r->isStatic && !m.manifestPath.empty()) {
            out << "  signature : " << SignatureStatusName(r->signature.status);
            if (!r->signature.publisher.empty()) out << " (" << r->signature.publisher << ")";
            if (r->installed) out << ", installed from a registry";
            out << "\n";
        }
        out << "  affects the simulation : " << (m.affectsSimulation ? "yes" : "no") << ", hash " << Hex(r->identityHash) << "\n";
        if (r->context) {
            out << "  registered :";
            for (const auto& [kind, count] : r->context->Summary()) out << " " << kind << " x" << count;
            out << "\n";
        }
        if (m.budgetMemoryMB > 0 || r->memoryMB > 0) out << "  counted memory " << r->memoryMB << " MB" << (m.budgetMemoryMB > 0 ? " of " + std::to_string(m.budgetMemoryMB) + " MB" + (m.enforceBudgets ? " (enforced)" : "") : std::string()) << "\n";
        if (m.budgetFrameMs > 0) out << "  frame budget " << m.budgetFrameMs << " ms, using " << (r->systemsMsAverage + r->frameMsAverage) << " ms\n";
        if (r->errorCount) out << "  errors caught : " << r->errorCount << "\n";
        if (r->reloadCount) out << "  reloaded " << r->reloadCount << " times\n";
        if (!r->error.empty()) out << "  failed : " << r->error << "\n";
        for (const std::string& w : r->warnings) out << "  warning : " << w << "\n";
        return out.str();
    }

    json PluginManager::DescribeJson() const
    {
        json out = json::object();
        json list = json::array();
        for (const auto& r : m_Records) {
            json p;
            p["id"] = r->manifest.id;
            p["version"] = r->manifest.version.ToString();
            p["state"] = PluginStateName(r->state);
            p["kind"] = PluginKindName(r->manifest.kind);
            p["moduleKind"] = ModuleKindName(r->manifest.moduleKind);
            p["tier"] = int(r->manifest.tier);
            p["order"] = r->owner;
            p["static"] = r->isStatic;
            p["affectsSimulation"] = r->manifest.affectsSimulation;
            p["hash"] = Hex(r->identityHash);
            if (!r->error.empty()) p["error"] = r->error;
            p["frameMs"] = r->systemsMsAverage + r->frameMsAverage;
            list.push_back(std::move(p));
        }
        out["plugins"] = list;
        json rejected = json::array();
        for (const Rejection& rej : m_Rejections) rejected.push_back({ { "id", rej.id }, { "version", rej.version }, { "reason", rej.reason } });
        out["rejected"] = rejected;
        out["pluginSetHash"] = Hex(SimulationPluginHash());
        return out;
    }

    // ---- Console ----

    void PluginManager::RegisterConsole()
    {
        Core::CVarRegistry& reg = Core::CVarRegistry::Global();
        auto add = [&](const std::string& name, const std::string& help, Core::CVarRegistry::CommandFn fn) {
            reg.RegisterCommand(name, help, std::move(fn));
            m_ConsoleCommands.push_back(name);
        };
        using Args = std::vector<std::string>;
        using R = Core::CommandResult;

        add("plugin.list", "plugin.list : the plugins, their state and what they cost", [this](const Args&) { return R{ true, Describe() }; });
        add("plugin.info", "plugin.info <id> : everything about one plugin", [this](const Args& a) {
            if (a.empty()) return R{ false, "usage : plugin.info <id>" };
            return R{ true, DescribePlugin(a[0]) };
        });
        add("plugin.enable", "plugin.enable <id> : starts a plugin (and what it needs)", [this](const Args& a) {
            if (a.empty()) return R{ false, "usage : plugin.enable <id>" };
            std::string e;
            return Enable(a[0], &e) ? R{ true, a[0] + " enabled" } : R{ false, e };
        });
        add("plugin.disable", "plugin.disable <id> : stops a plugin and what depends on it, and removes what it registered", [this](const Args& a) {
            if (a.empty()) return R{ false, "usage : plugin.disable <id>" };
            std::string e;
            return Disable(a[0], true, &e) ? R{ true, a[0] + " disabled" } : R{ false, e };
        });
        add("plugin.reload", "plugin.reload <id> : rebuilds a plugin from its library on disk (dev builds)", [this](const Args& a) {
            if (a.empty()) return R{ false, "usage : plugin.reload <id>" };
            std::string e;
            return Reload(a[0], &e) ? R{ true, a[0] + " reloaded" } : R{ false, e };
        });
        add("plugin.services", "plugin.services : the services plugins offer", [this](const Args&) {
            std::ostringstream out;
            for (const auto& s : m_Services.List()) {
                std::string owner = "?";
                for (const auto& r : m_Records) if (r->owner == s.owner) owner = r->manifest.id;
                out << s.name << " " << s.version.ToString() << "  (" << owner << ")\n";
            }
            const std::string text = out.str();
            return R{ true, text.empty() ? "no services" : text };
        });
        add("plugin.hooks", "plugin.hooks : how many handlers plugins attached to the hooks", [this](const Args&) {
            return R{ true, std::to_string(m_Hooks.Count()) + " hook handlers" };
        });
        add("plugin.extensions", "plugin.extensions [point] : the items plugins added to extension points", [this](const Args& a) {
            std::ostringstream out;
            for (const auto& item : m_Extensions.List(a.empty() ? std::string() : a[0])) {
                std::string owner = "?";
                for (const auto& r : m_Records) if (r->owner == item.owner) owner = r->manifest.id;
                out << item.point << " : " << item.name << "  (" << owner << ")\n";
            }
            const std::string text = out.str();
            return R{ true, text.empty() ? "nothing" : text };
        });
        add("plugin.content", "plugin.content [id] : the files of the modules (all layers when no id)", [this](const Args& a) {
            std::ostringstream out;
            if (a.empty()) {
                for (const auto& f : m_Content.ListLogical()) {
                    auto res = m_Content.ResolveLogical(f);
                    out << f << "  <- " << (res ? res->pluginId : "?") << "\n";
                }
            } else {
                for (const auto& f : m_Content.List(a[0])) out << ContentMounts::MakeModUri(a[0], f) << "\n";
            }
            const std::string text = out.str();
            return R{ true, text.empty() ? "no content" : text };
        });
        add("plugin.conflicts", "plugin.conflicts : files two modules provide without an override", [this](const Args&) {
            std::ostringstream out;
            for (const auto& c : m_Content.FindConflicts()) out << c.logicalPath << " : " << c.keptFrom << " kept, " << c.rejectedFrom << " ignored\n";
            const std::string text = out.str();
            return R{ true, text.empty() ? "no conflicts" : text };
        });
        add("plugin.hash", "plugin.hash : the hash of the simulation plugins (what a replay or a server compares)", [this](const Args&) {
            std::ostringstream out;
            out << Hex(SimulationPluginHash()) << "\n";
            for (const auto& e : SimulationPluginSet()) out << "  " << e.id << " " << e.version << " " << Hex(e.hash) << "\n";
            return R{ true, out.str() };
        });
        add("plugin.lock", "plugin.lock [verify|write] : check or rewrite plugins.lock", [this](const Args& a) {
            if (m_Config.lockfilePath.empty()) return R{ false, "this run has no project, so no plugins.lock" };
            const std::string sub = a.empty() ? "verify" : a[0];
            if (sub == "write") return WriteLockfile(m_Config.lockfilePath) ? R{ true, "wrote " + m_Config.lockfilePath } : R{ false, "could not write " + m_Config.lockfilePath };
            std::string text;
            {
                std::ifstream in(m_Config.lockfilePath);
                std::stringstream ss; ss << in.rdbuf(); text = ss.str();
            }
            if (text.empty()) return R{ false, "no plugins.lock yet (plugin.lock write)" };
            json lock = json::parse(text, nullptr, false);
            if (lock.is_discarded()) return R{ false, "plugins.lock is not valid JSON" };
            const auto problems = VerifyLockfile(lock);
            if (problems.empty()) return R{ true, "plugins.lock matches" };
            std::string all;
            for (const auto& p : problems) all += p + "\n";
            return R{ false, all };
        });
        add("plugin.validate", "plugin.validate <folder> : checks a plugin or datapack folder without loading it", [](const Args& a) {
            if (a.empty()) return R{ false, "usage : plugin.validate <folder>" };
            std::ifstream in(a[0] + "/shard-plugin.json");
            if (!in) return R{ false, "no shard-plugin.json in " + a[0] };
            std::stringstream ss; ss << in.rdbuf();
            auto manifest = ParseManifestText(ss.str());
            if (!manifest) return R{ false, "manifest : " + manifest.GetError().message };
            std::string out = "manifest ok : " + manifest.Value().id + " " + manifest.Value().version.ToString() + "\n";
            const std::string root = a[0] + "/" + manifest.Value().contentMount;
            if (manifest.Value().HasContent() && (fs::is_directory(root) || manifest.Value().moduleKind == ModuleKind::Datapack || manifest.Value().moduleKind == ModuleKind::Mod)) {
                const auto problems = ValidateContentFolder(a[0] + "/" + manifest.Value().contentMount);
                for (const auto& p : problems) out += p + "\n";
                if (problems.empty()) out += "content ok\n";
                return R{ problems.empty(), out };
            }
            return R{ true, out };
        });
        add("plugin.search", "plugin.search [text] : plugins the configured registries offer", [this](const Args& a) {
            auto registries = LoadRegistries();
            if (!registries) return R{ false, registries.GetError().message };
            std::ostringstream out;
            for (const Registry& r : registries.Value())
                for (const RegistryVersion& v : r.Search(a.empty() ? std::string() : a[0]))
                    out << v.id << " " << v.version.ToString() << "  " << v.name << (v.publisher.empty() ? "" : "  by " + v.publisher) << "  [" << r.Name() << "]\n";
            const std::string text = out.str();
            return R{ true, text.empty() ? "nothing found" : text };
        });
        add("plugin.install", "plugin.install <id> [range] : installs a plugin and what it needs from a registry (it loads on the next start)", [this](const Args& a) {
            if (a.empty()) return R{ false, "usage : plugin.install <id> [range]" };
            auto installed = InstallPlugin(a[0], a.size() > 1 ? a[1] : std::string("*"));
            if (!installed) return R{ false, installed.GetError().message };
            std::string text;
            for (const InstalledPlugin& p : installed.Value()) text += "installed " + p.id + " " + p.version + (p.publisher.empty() ? "" : " (by " + p.publisher + ")") + "\n";
            return R{ true, text + "restart to load them" };
        });
        add("plugin.installed", "plugin.installed : the plugins installed from registries", [this](const Args&) {
            std::ostringstream out;
            for (const InstalledPlugin& p : PluginInstaller::ListInstalled(m_InstallDirectory))
                out << p.id << " " << p.version << "  (" << p.source << ")\n";
            const std::string text = out.str();
            return R{ true, text.empty() ? "nothing installed from a registry" : text };
        });
        add("plugin.uninstall", "plugin.uninstall <id> : removes a plugin installed from a registry (not while it is loaded)", [this](const Args& a) {
            if (a.empty()) return R{ false, "usage : plugin.uninstall <id>" };
            if (const PluginRecord* r = Find(a[0]); r && r->IsLive()) return R{ false, a[0] + " is loaded : disable it and restart first" };
            auto removed = PluginInstaller::Uninstall(m_InstallDirectory, a[0]);
            return removed ? R{ true, "removed " + a[0] } : R{ false, removed.GetError().message };
        });
        add("plugin.updates", "plugin.updates : installed plugins a registry has a newer version of (same major)", [this](const Args&) {
            auto registries = LoadRegistries();
            if (!registries) return R{ false, registries.GetError().message };
            PluginInstaller installer(std::move(registries.Value()));
            std::ostringstream out;
            for (const auto& [installed, newer] : installer.FindUpdates(m_InstallDirectory, m_Config.engineVersion))
                out << installed.id << " " << installed.version << " -> " << newer.version.ToString() << "\n";
            const std::string text = out.str();
            return R{ true, text.empty() ? "everything is up to date" : text };
        });
        add("plugin.update", "plugin.update <id> : installs the newest version a registry has, same major", [this](const Args& a) {
            if (a.empty()) return R{ false, "usage : plugin.update <id>" };
            auto registries = LoadRegistries();
            if (!registries) return R{ false, registries.GetError().message };
            PluginInstaller installer(std::move(registries.Value()));
            for (const auto& [installed, newer] : installer.FindUpdates(m_InstallDirectory, m_Config.engineVersion)) {
                if (installed.id != a[0]) continue;
                auto done = InstallPlugin(installed.id, newer.version.ToString());
                return done ? R{ true, "updated " + installed.id + " to " + newer.version.ToString() + " : restart to load it" } : R{ false, done.GetError().message };
            }
            return R{ true, a[0] + " is up to date" };
        });
        add("plugin.trust", "plugin.trust <publisher> <public key> : accepts the signatures of a publisher", [this](const Args& a) {
            if (a.size() < 2) return R{ false, "usage : plugin.trust <publisher> <64 hex digits>" };
            auto done = TrustPublisher(a[0], a[1]);
            return done ? R{ true, "trusting " + a[0] } : R{ false, done.GetError().message };
        });
        add("plugin.verify", "plugin.verify <id> : who signed a plugin, and whether its files are the signed ones", [this](const Args& a) {
            if (a.empty()) return R{ false, "usage : plugin.verify <id>" };
            const PluginRecord* r = nullptr;
            for (const auto& rec : m_Records) if (rec->manifest.id == a[0]) r = rec.get();
            if (!r) return R{ false, "no plugin '" + a[0] + "'" };
            if (r->manifest.rootDir.empty() || r->isStatic) return R{ true, a[0] + " is part of the executable" };
            const SignatureInfo sig = VerifyPackage(r->manifest.rootDir, m_Trust);
            return R{ sig.status == SignatureInfo::Status::Valid || sig.status == SignatureInfo::Status::Unsigned, std::string(SignatureStatusName(sig.status)) + (sig.message.empty() ? "" : " : " + sig.message) };
        });
        add("plugin.budgets", "plugin.budgets : time per frame of each plugin, against its budget", [this](const Args&) {
            UpdateBudgets();
            std::ostringstream out;
            for (const auto& r : m_Records) {
                if (!r->IsRunning() || r->isStatic) continue;
                out << std::left << std::setw(34) << r->manifest.id << std::fixed << std::setprecision(3) << (r->systemsMsAverage + r->frameMsAverage) << " ms";
                if (r->manifest.budgetFrameMs > 0) out << " / " << r->manifest.budgetFrameMs << " ms" << (r->overBudget ? "  OVER" : "");
                if (r->context && (r->manifest.budgetMemoryMB > 0 || r->memoryMB > 0)) out << "   " << r->memoryMB << " MB" << (r->manifest.budgetMemoryMB > 0 ? " / " + std::to_string(r->manifest.budgetMemoryMB) + " MB" : std::string()) << (r->overMemoryBudget ? "  OVER" : "");
                out << "\n";
            }
            const std::string text = out.str();
            return R{ true, text.empty() ? "no plugins running" : text };
        });
    }

    void PluginManager::UnregisterConsole()
    {
        Core::CVarRegistry& reg = Core::CVarRegistry::Global();
        for (const std::string& name : m_ConsoleCommands) reg.UnregisterCommand(name);
        m_ConsoleCommands.clear();
    }
}
