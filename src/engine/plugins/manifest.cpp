#include "engine/plugins/manifest.hpp"

#include <algorithm>
#include <cctype>

namespace Shard::Engine::Plugins {

    using nlohmann::json;

    const char* HostName(Host host)
    {
        switch (host) {
            case Host::Engine: return "engine";
            case Host::Editor: return "editor";
            case Host::Player: return "player";
            case Host::Headless: return "headless";
            case Host::Tool: return "tool";
        }
        return "?";
    }

    bool ParseHost(const std::string& name, Host& out)
    {
        for (Host h : { Host::Engine, Host::Editor, Host::Player, Host::Headless, Host::Tool }) {
            if (name == HostName(h)) { out = h; return true; }
        }
        return false;
    }

    const char* PluginKindName(PluginKind kind) { return kind == PluginKind::Module ? "module" : "plugin"; }

    const char* ModuleKindName(ModuleKind kind)
    {
        switch (kind) {
            case ModuleKind::None: return "none";
            case ModuleKind::Engine: return "engine";
            case ModuleKind::Genre: return "genre";
            case ModuleKind::Code: return "code";
            case ModuleKind::Mod: return "mod";
            case ModuleKind::Datapack: return "datapack";
        }
        return "?";
    }

    std::string CurrentPlatformKey()
    {
#if defined(_WIN32)
        const char* os = "windows";
#elif defined(__APPLE__)
        const char* os = "macos";
#else
        const char* os = "linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
        const char* arch = "arm64";
#else
        const char* arch = "x86_64";
#endif
        return std::string(os) + "-" + arch;
    }

    std::string PluginManifest::BinaryForThisPlatform() const
    {
        auto it = binaries.find(CurrentPlatformKey());
        return it == binaries.end() ? std::string() : it->second;
    }

    bool IsValidPluginId(const std::string& id)
    {
        if (id.empty() || id.size() > 128) return false;
        bool segmentStart = true;
        for (char c : id) {
            if (c == '.') {
                if (segmentStart) return false;         // empty segment
                segmentStart = true;
                continue;
            }
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
            if (!ok) return false;
            segmentStart = false;
        }
        return !segmentStart;
    }

    namespace {
        struct Reader {
            const json& j;
            std::vector<std::string>& errors;

            bool Has(const char* key) const { return j.contains(key) && !j[key].is_null(); }

            std::string String(const char* key, std::string fallback = {}) const
            {
                if (!Has(key)) return fallback;
                if (!j[key].is_string()) { errors.push_back(std::string("'") + key + "' must be a string"); return fallback; }
                return j[key].get<std::string>();
            }

            bool Bool(const char* key, bool fallback) const
            {
                if (!Has(key)) return fallback;
                if (!j[key].is_boolean()) { errors.push_back(std::string("'") + key + "' must be true or false"); return fallback; }
                return j[key].get<bool>();
            }

            std::vector<std::string> Strings(const char* key) const
            {
                std::vector<std::string> out;
                if (!Has(key)) return out;
                if (!j[key].is_array()) { errors.push_back(std::string("'") + key + "' must be an array of strings"); return out; }
                for (const json& v : j[key]) {
                    if (!v.is_string()) { errors.push_back(std::string("'") + key + "' must only hold strings"); continue; }
                    out.push_back(v.get<std::string>());
                }
                return out;
            }
        };

        void ReadDependencies(const json& arr, const char* key, std::vector<DependencySpec>& out, std::vector<std::string>& errors)
        {
            if (!arr.is_array()) { errors.push_back(std::string("'") + key + "' must be an array"); return; }
            for (const json& d : arr) {
                DependencySpec spec;
                if (d.is_string()) {
                    spec.id = d.get<std::string>();
                } else if (d.is_object() && d.contains("id") && d["id"].is_string()) {
                    spec.id = d["id"].get<std::string>();
                    if (d.contains("version")) {
                        if (!d["version"].is_string()) { errors.push_back(std::string(key) + " '" + spec.id + "' : version must be a string"); continue; }
                        std::string err;
                        auto range = VersionRange::Parse(d["version"].get<std::string>(), &err);
                        if (!range) { errors.push_back(std::string(key) + " '" + spec.id + "' : " + err); continue; }
                        spec.range = *range;
                    }
                    if (d.contains("optional") && d["optional"].is_boolean()) spec.optional = d["optional"].get<bool>();
                } else {
                    errors.push_back(std::string("'") + key + "' entries are an id or an object with an id");
                    continue;
                }
                // "service:<name>" : any plugin that provides the service
                const bool isService = spec.id.compare(0, 8, "service:") == 0;
                if (!IsValidPluginId(isService ? spec.id.substr(8) : spec.id)) { errors.push_back(std::string(key) + " : '" + spec.id + "' is not a valid plugin id"); continue; }
                out.push_back(std::move(spec));
            }
        }
    }

    Core::Result<PluginManifest> ParseManifest(const json& j)
    {
        using namespace Core;
        std::vector<std::string> errors;
        if (!j.is_object()) return Err("the manifest must be a JSON object");

        Reader r{ j, errors };
        PluginManifest m;

        m.schema = j.value("schema", 1u);
        if (m.schema != 1) errors.push_back("unsupported manifest schema " + std::to_string(m.schema) + " (this engine reads schema 1)");

        m.id = r.String("id");
        if (m.id.empty()) errors.push_back("'id' is required");
        else if (!IsValidPluginId(m.id)) errors.push_back("'" + m.id + "' is not a valid id (lowercase letters, digits, '-' and '_', separated by dots)");

        m.name = r.String("name", m.id);
        m.description = r.String("description");
        m.authors = r.Strings("authors");

        const std::string versionText = r.String("version");
        if (versionText.empty()) errors.push_back("'version' is required");
        else if (auto v = SemVer::Parse(versionText)) m.version = *v;
        else errors.push_back("'version' : '" + versionText + "' is not a semantic version (major.minor.patch)");

        // kind / moduleKind
        const std::string kind = r.String("kind", "plugin");
        if (kind == "plugin") m.kind = PluginKind::Plugin;
        else if (kind == "module") m.kind = PluginKind::Module;
        else errors.push_back("'kind' must be \"plugin\" or \"module\"");

        const std::string moduleKind = r.String("moduleKind");
        if (!moduleKind.empty()) {
            if (moduleKind == "engine") m.moduleKind = ModuleKind::Engine;
            else if (moduleKind == "genre") m.moduleKind = ModuleKind::Genre;
            else if (moduleKind == "code") m.moduleKind = ModuleKind::Code;
            else if (moduleKind == "mod") m.moduleKind = ModuleKind::Mod;
            else if (moduleKind == "datapack") m.moduleKind = ModuleKind::Datapack;
            else errors.push_back("'moduleKind' must be engine, genre, code, mod or datapack");
        }
        if (m.kind == PluginKind::Module && m.moduleKind == ModuleKind::None) m.moduleKind = ModuleKind::Code;
        if (m.kind == PluginKind::Plugin && m.moduleKind != ModuleKind::None)
            errors.push_back("'moduleKind' is only for modules (set \"kind\": \"module\")");

        const std::string linkage = r.String("linkage", m.moduleKind == ModuleKind::Engine ? "static" : "dynamic");
        if (linkage == "static") m.linkage = Linkage::Static;
        else if (linkage == "dynamic") m.linkage = Linkage::Dynamic;
        else errors.push_back("'linkage' must be \"static\" or \"dynamic\"");

        // tier
        if (m.moduleKind == ModuleKind::Datapack) m.tier = TrustTier::Data;
        else if (m.linkage == Linkage::Static || m.moduleKind == ModuleKind::Engine) m.tier = TrustTier::FirstParty;
        else m.tier = TrustTier::FirstParty;
        if (r.Has("tier")) {
            if (!j["tier"].is_number_integer() || j["tier"].get<int>() < 0 || j["tier"].get<int>() > 3) errors.push_back("'tier' must be 0, 1, 2 or 3");
            else m.tier = static_cast<TrustTier>(j["tier"].get<int>());
        }
        if (m.moduleKind == ModuleKind::Datapack && m.tier != TrustTier::Data) errors.push_back("a datapack is tier 3 (it has no code)");
        if (m.tier == TrustTier::Data && m.moduleKind != ModuleKind::Datapack) errors.push_back("tier 3 is for datapacks");

        // hosts
        if (r.Has("hosts")) {
            m.hosts = 0;
            for (const std::string& name : r.Strings("hosts")) {
                Host h;
                if (!ParseHost(name, h)) { errors.push_back("unknown host '" + name + "' (engine, editor, player, headless, tool)"); continue; }
                m.hosts |= (h == Host::Engine) ? kAllSimulationHosts : HostBit(h);
            }
            if (m.hosts == 0 && errors.empty()) errors.push_back("'hosts' is empty : the plugin would never load");
        } else if (m.moduleKind == ModuleKind::Datapack) {
            m.hosts = kAllSimulationHosts | HostBit(Host::Tool);
        }

        // engine
        if (r.Has("engine")) {
            const json& e = j["engine"];
            if (!e.is_object()) errors.push_back("'engine' must be an object");
            else {
                if (e.contains("api")) {
                    const std::string api = e["api"].is_string() ? e["api"].get<std::string>() : std::string();
                    const size_t dot = api.find('.');
                    try {
                        m.apiMajor = uint32_t(std::stoul(api.substr(0, dot)));
                        m.apiMinor = dot == std::string::npos ? 0 : uint32_t(std::stoul(api.substr(dot + 1)));
                    } catch (...) { errors.push_back("'engine.api' must look like \"1.2\""); }
                }
                if (e.contains("versions")) {
                    std::string err;
                    const std::string text = e["versions"].is_string() ? e["versions"].get<std::string>() : std::string();
                    if (auto range = VersionRange::Parse(text, &err)) m.engineVersions = *range;
                    else errors.push_back("'engine.versions' : " + err);
                }
                if (e.contains("requires")) {
                    if (!e["requires"].is_array()) errors.push_back("'engine.requires' must be an array");
                    else for (const json& f : e["requires"]) if (f.is_string()) m.engineRequires.push_back(f.get<std::string>());
                }
            }
        }
        m.abiFingerprint = r.String("abiFingerprint");

        // binaries
        if (r.Has("binaries")) {
            if (!j["binaries"].is_object()) errors.push_back("'binaries' must map a platform to a path");
            else for (auto it = j["binaries"].begin(); it != j["binaries"].end(); ++it) {
                if (!it.value().is_string()) { errors.push_back("'binaries." + it.key() + "' must be a path"); continue; }
                m.binaries[it.key()] = it.value().get<std::string>();
            }
        }
        m.entry = r.String("entry", m.entry);
        if (m.moduleKind == ModuleKind::Datapack && !m.binaries.empty()) errors.push_back("a datapack has no binaries");

        // dependency lists
        if (j.contains("dependencies")) ReadDependencies(j["dependencies"], "dependencies", m.dependencies, errors);
        if (j.contains("conflicts")) ReadDependencies(j["conflicts"], "conflicts", m.conflicts, errors);
        for (const DependencySpec& c : m.conflicts) if (c.id == m.id) errors.push_back("a plugin cannot conflict with itself");
        for (const DependencySpec& d : m.dependencies) if (d.id == m.id) errors.push_back("a plugin cannot depend on itself");

        if (j.contains("provides")) {
            if (!j["provides"].is_array()) errors.push_back("'provides' must be an array");
            else for (const json& p : j["provides"]) {
                if (!p.is_object() || !p.contains("service") || !p["service"].is_string()) { errors.push_back("'provides' entries need a \"service\" name"); continue; }
                ProvidedService s;
                s.service = p["service"].get<std::string>();
                s.version = SemVer{ 1, 0, 0, {} };
                if (p.contains("version") && p["version"].is_string()) {
                    std::string text = p["version"].get<std::string>();
                    // "1.0" is a fine way to write a service version
                    if (std::count(text.begin(), text.end(), '.') == 1) text += ".0";
                    if (auto v = SemVer::Parse(text)) s.version = *v;
                    else { errors.push_back("service '" + s.service + "' : bad version"); continue; }
                }
                m.provides.push_back(std::move(s));
            }
        }
        m.loadAfter = r.Strings("loadAfter");
        m.loadBefore = r.Strings("loadBefore");

        if (r.Has("simulation")) {
            const json& s = j["simulation"];
            if (!s.is_object()) errors.push_back("'simulation' must be an object");
            else {
                m.affectsSimulation = s.value("affects", false);
                m.deterministic = s.value("deterministic", true);
            }
        } else {
            // A code module changes the game ; a plugin for the editor or the tools does not
            m.affectsSimulation = m.moduleKind == ModuleKind::Code || m.moduleKind == ModuleKind::Genre || m.moduleKind == ModuleKind::Mod || m.moduleKind == ModuleKind::Datapack;
        }

        if (r.Has("content")) {
            const json& c = j["content"];
            if (!c.is_object()) errors.push_back("'content' must be an object");
            else {
                m.contentMount = c.value("mount", std::string("content"));
                m.assetNamespace = c.value("assetNamespace", std::string());
                if (c.contains("overrides") && c["overrides"].is_array())
                    for (const json& o : c["overrides"]) if (o.is_string()) m.overrides.push_back(o.get<std::string>());
            }
        } else if (m.kind == PluginKind::Plugin) {
            m.contentMount.clear();         // a plain plugin has no content unless it says so
        }
        if (m.kind == PluginKind::Plugin && !m.contentMount.empty() && !r.Has("content")) m.contentMount.clear();
        if (m.contentMount.find("..") != std::string::npos || (!m.contentMount.empty() && (m.contentMount[0] == '/' || m.contentMount[0] == '\\')))
            errors.push_back("'content.mount' must be a relative path inside the plugin");

        if (r.Has("budgets")) {
            const json& b = j["budgets"];
            if (!b.is_object()) errors.push_back("'budgets' must be an object");
            else {
                m.budgetMemoryMB = b.value("memoryMB", 0.0);
                m.budgetFrameMs = b.value("frameMs", 0.0);
                m.enforceBudgets = b.value("enforce", false);
                if (m.budgetMemoryMB < 0 || m.budgetFrameMs < 0) errors.push_back("budgets cannot be negative");
            }
        }
        m.permissions = r.Strings("permissions");
        if (r.Has("settings")) {
            if (!j["settings"].is_object()) errors.push_back("'settings' must be an object");
            else m.settings = j["settings"];
        }

        if (!errors.empty()) {
            std::string all;
            for (size_t i = 0; i < errors.size(); ++i) all += (i ? "\n" : "") + errors[i];
            return Err(all);
        }
        return Ok(std::move(m));
    }

    Core::Result<PluginManifest> ParseManifestText(const std::string& text)
    {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded()) return Core::Err("the manifest is not valid JSON");
        return ParseManifest(j);
    }

    nlohmann::ordered_json ManifestToJson(const PluginManifest& m)
    {
        nlohmann::ordered_json j;
        j["schema"] = m.schema;
        j["id"] = m.id;
        j["name"] = m.name;
        if (!m.description.empty()) j["description"] = m.description;
        j["version"] = m.version.ToString();
        j["kind"] = PluginKindName(m.kind);
        if (m.moduleKind != ModuleKind::None) j["moduleKind"] = ModuleKindName(m.moduleKind);
        j["linkage"] = m.linkage == Linkage::Static ? "static" : "dynamic";
        j["tier"] = int(m.tier);
        auto hosts = nlohmann::ordered_json::array();
        for (Host h : { Host::Editor, Host::Player, Host::Headless, Host::Tool })
            if (m.RunsOn(h)) hosts.push_back(HostName(h));
        j["hosts"] = hosts;
        j["engine"]["api"] = std::to_string(m.apiMajor) + "." + std::to_string(m.apiMinor);
        j["engine"]["versions"] = m.engineVersions.Text();
        if (!m.engineRequires.empty()) j["engine"]["requires"] = m.engineRequires;
        if (!m.abiFingerprint.empty()) j["abiFingerprint"] = m.abiFingerprint;
        if (!m.binaries.empty()) j["binaries"] = m.binaries;
        auto deps = nlohmann::ordered_json::array();
        for (const DependencySpec& d : m.dependencies) {
            nlohmann::ordered_json e; e["id"] = d.id; e["version"] = d.range.Text(); if (d.optional) e["optional"] = true;
            deps.push_back(e);
        }
        j["dependencies"] = deps;
        j["simulation"]["affects"] = m.affectsSimulation;
        j["simulation"]["deterministic"] = m.deterministic;
        return j;
    }
}
