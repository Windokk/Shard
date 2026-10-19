#include "engine/plugins/content.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace Shard::Engine::Plugins {

    namespace fs = std::filesystem;
    using nlohmann::json;

    namespace {
        constexpr const char* kModScheme = "mod://";

        std::string Slash(std::string s)
        {
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        }

        std::string TrimSlashes(std::string s)
        {
            while (!s.empty() && s.back() == '/') s.pop_back();
            return s;
        }

        // True if `relative` stays inside the folder it is joined to (no "..", not absolute)
        bool IsSafeRelative(const std::string& relative)
        {
            if (relative.empty()) return false;
            if (relative[0] == '/' || relative.find(':') != std::string::npos) return false;
            std::stringstream ss(relative);
            std::string part;
            while (std::getline(ss, part, '/'))
                if (part == "..") return false;
            return true;
        }

        std::optional<std::string> ReadFile(const std::string& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) return std::nullopt;
            std::stringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        void ListFiles(const std::string& root, const std::string& subdir, std::vector<std::string>& out)
        {
            std::error_code ec;
            const fs::path base = subdir.empty() ? fs::path(root) : fs::path(root) / subdir;
            if (!fs::is_directory(base, ec)) return;
            for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
                if (ec) break;
                if (!it->is_regular_file(ec)) continue;
                out.push_back(Slash(fs::relative(it->path(), root, ec).generic_string()));
            }
        }

        void MergeInto(json& target, const json& source)
        {
            if (target.is_object() && source.is_object()) {
                for (auto it = source.begin(); it != source.end(); ++it) {
                    if (target.contains(it.key())) MergeInto(target[it.key()], it.value());
                    else target[it.key()] = it.value();
                }
            } else {
                target = source;
            }
        }
    }

    // ---- ContentMounts ----

    std::string ContentMounts::Normalize(const std::string& path)
    {
        std::string s = Slash(path);
        while (!s.empty() && s[0] == '/') s.erase(0, 1);
        return s;
    }

    bool ContentMounts::Mount(const PluginManifest& manifest, PluginOwner owner, std::string* error)
    {
        if (!manifest.HasContent()) return true;
        const fs::path root = fs::path(manifest.rootDir) / manifest.contentMount;
        std::error_code ec;
        if (!fs::is_directory(root, ec)) {
            // A module whose content folder is missing is not an error when it never promised content (a code module)
            if (manifest.moduleKind == ModuleKind::Datapack || manifest.moduleKind == ModuleKind::Mod) {
                if (error) *error = "content folder '" + manifest.contentMount + "' is missing";
                return false;
            }
            return true;
        }
        if (Find(manifest.id)) {
            if (error) *error = "content of '" + manifest.id + "' is already mounted";
            return false;
        }
        MountPoint m;
        m.pluginId = manifest.id;
        m.ns = manifest.Namespace();
        m.root = TrimSlashes(Slash(fs::absolute(root, ec).lexically_normal().generic_string()));
        m.owner = owner;
        m.kind = manifest.moduleKind;
        m.overrides = manifest.overrides;
        m_Mounts.push_back(std::move(m));
        return true;
    }

    bool ContentMounts::MountFolder(const std::string& id, const std::string& folder, PluginOwner owner, std::vector<std::string> overrides)
    {
        if (Find(id)) return false;
        std::error_code ec;
        if (!fs::is_directory(folder, ec)) return false;
        MountPoint m;
        m.pluginId = id;
        m.ns = id;
        m.root = TrimSlashes(Slash(fs::absolute(folder, ec).lexically_normal().generic_string()));
        m.owner = owner;
        m.overrides = std::move(overrides);
        m_Mounts.push_back(std::move(m));
        return true;
    }

    size_t ContentMounts::Unmount(PluginOwner owner)
    {
        const size_t before = m_Mounts.size();
        m_Mounts.erase(std::remove_if(m_Mounts.begin(), m_Mounts.end(), [&](const MountPoint& m) { return m.owner == owner; }), m_Mounts.end());
        return before - m_Mounts.size();
    }

    void ContentMounts::Clear() { m_Mounts.clear(); }

    const ContentMounts::MountPoint* ContentMounts::Find(const std::string& pluginId) const
    {
        for (const MountPoint& m : m_Mounts) if (m.pluginId == pluginId) return &m;
        return nullptr;
    }

    bool ContentMounts::IsModUri(const std::string& uri) { return uri.compare(0, 6, kModScheme) == 0; }

    bool ContentMounts::ParseModUri(const std::string& uri, std::string& pluginId, std::string& path)
    {
        if (!IsModUri(uri)) return false;
        const std::string rest = Slash(uri.substr(6));
        const size_t slash = rest.find('/');
        if (slash == std::string::npos || slash == 0) return false;
        pluginId = rest.substr(0, slash);
        path = rest.substr(slash + 1);
        return !path.empty();
    }

    std::string ContentMounts::MakeModUri(const std::string& pluginId, const std::string& path)
    {
        return std::string(kModScheme) + pluginId + "/" + Normalize(path);
    }

    std::optional<std::string> ContentMounts::Resolve(const std::string& uri) const
    {
        std::string id, path;
        if (!ParseModUri(uri, id, path) || !IsSafeRelative(path)) return std::nullopt;
        const MountPoint* m = Find(id);
        if (!m) return std::nullopt;
        const std::string full = m->root + "/" + path;
        std::error_code ec;
        if (!fs::is_regular_file(full, ec)) return std::nullopt;
        return full;
    }

    std::optional<std::string> ContentMounts::ReadText(const std::string& uri) const
    {
        auto file = Resolve(uri);
        return file ? ReadFile(*file) : std::nullopt;
    }

    std::vector<std::string> ContentMounts::List(const std::string& pluginId, const std::string& subdir) const
    {
        std::vector<std::string> out;
        const MountPoint* m = Find(pluginId);
        if (!m || (!subdir.empty() && !IsSafeRelative(Normalize(subdir)))) return out;
        ListFiles(m->root, Normalize(subdir), out);
        std::sort(out.begin(), out.end());
        return out;
    }

    bool ContentMounts::Overrides(const MountPoint& mount, const std::string& logicalPath) const
    {
        for (const std::string& o : mount.overrides) {
            if (o == "*" || o == logicalPath) return true;
            if (!o.empty() && o.back() == '*' && logicalPath.compare(0, o.size() - 1, o, 0, o.size() - 1) == 0) return true;      // "materials/*"
        }
        return false;
    }

    std::optional<ContentMounts::Resolved> ContentMounts::ResolveLogical(const std::string& logicalPath) const
    {
        const std::string path = Normalize(logicalPath);
        if (!IsSafeRelative(path)) return std::nullopt;
        const MountPoint* winner = nullptr;
        for (const MountPoint& m : m_Mounts) {
            std::error_code ec;
            if (!fs::is_regular_file(m.root + "/" + path, ec)) continue;
            if (!winner || Overrides(m, path)) winner = &m;      // a later layer replaces an earlier one only when it says so
        }
        if (!winner) return std::nullopt;
        return Resolved{ winner->pluginId, winner->root + "/" + path, MakeModUri(winner->pluginId, path) };
    }

    std::vector<std::string> ContentMounts::ListLogical(const std::string& subdir) const
    {
        std::set<std::string> all;
        for (const MountPoint& m : m_Mounts) {
            std::vector<std::string> files;
            ListFiles(m.root, Normalize(subdir), files);
            for (auto& f : files) all.insert(f);
        }
        return { all.begin(), all.end() };
    }

    std::vector<ContentConflict> ContentMounts::FindConflicts() const
    {
        std::vector<ContentConflict> out;
        std::map<std::string, const MountPoint*> kept;
        for (const MountPoint& m : m_Mounts) {
            std::vector<std::string> files;
            ListFiles(m.root, {}, files);
            std::sort(files.begin(), files.end());
            for (const std::string& f : files) {
                // Data that is merged on purpose never clashes
                if (f.compare(0, 7, "tables/") == 0 || f.compare(0, 5, "lang/") == 0 || f.compare(0, 8, "patches/") == 0) continue;
                auto it = kept.find(f);
                if (it == kept.end()) { kept[f] = &m; continue; }
                if (Overrides(m, f)) { it->second = &m; continue; }
                out.push_back({ f, it->second->pluginId, m.pluginId });
            }
        }
        return out;
    }

    Core::Guid ContentMounts::AssetGuid(const std::string& assetNamespace, const std::string& path)
    {
        // A fixed namespace for every module asset, so that "<ns>/<path>" is all that decides the id
        static const Core::Guid kModuleAssets = Core::Guid::FromName("shard.module-assets");
        std::string name = assetNamespace + "/" + Normalize(path);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return Core::Guid::FromName(kModuleAssets, name);
    }

    Core::Guid ContentMounts::AssetGuid(const std::string& pluginId, const std::string& path, bool* found) const
    {
        const MountPoint* m = Find(pluginId);
        if (found) *found = m != nullptr;
        return m ? AssetGuid(m->ns, path) : Core::Guid();
    }

    json ContentMounts::LoadTable(const std::string& name) const
    {
        json merged = json::object();
        for (const MountPoint& m : m_Mounts) {
            auto text = ReadFile(m.root + "/tables/" + name + ".json");
            if (!text) continue;
            json part = json::parse(*text, nullptr, false);
            if (part.is_discarded()) continue;
            MergeInto(merged, part);
        }
        return merged;
    }

    std::vector<std::string> ContentMounts::TableNames() const
    {
        std::set<std::string> names;
        for (const MountPoint& m : m_Mounts) {
            std::vector<std::string> files;
            ListFiles(m.root, "tables", files);
            for (const std::string& f : files)
                if (f.size() > 12 && f.compare(f.size() - 5, 5, ".json") == 0) names.insert(f.substr(7, f.size() - 12));
        }
        return { names.begin(), names.end() };
    }

    json ContentMounts::LoadLanguage(const std::string& language) const
    {
        json merged = json::object();
        for (const MountPoint& m : m_Mounts) {
            auto text = ReadFile(m.root + "/lang/" + language + ".json");
            if (!text) continue;
            json part = json::parse(*text, nullptr, false);
            if (part.is_discarded() || !part.is_object()) continue;
            MergeInto(merged, part);
        }
        return merged;
    }

    std::string ContentMounts::Translate(const std::string& key, const std::string& language, const std::string& fallback) const
    {
        const json table = LoadLanguage(language);
        auto lookup = [&](const json& t) -> std::optional<std::string> {
            if (t.contains(key) && t[key].is_string()) return t[key].get<std::string>();
            // "ui.start" may also be written nested : { "ui": { "start": "..." } }
            const json* node = &t;
            std::stringstream ss(key);
            std::string part;
            while (std::getline(ss, part, '.')) {
                if (!node->is_object() || !node->contains(part)) return std::nullopt;
                node = &(*node)[part];
            }
            if (node->is_string()) return node->get<std::string>();
            return std::nullopt;
        };
        if (auto v = lookup(table)) return *v;
        return fallback.empty() ? key : fallback;
    }

    // ---- Validation ----

    namespace {
        void Problem(std::vector<std::string>& out, const std::string& file, const std::string& message)
        {
            out.push_back(file + " : " + message);
        }

        json ReadJson(const std::string& path, std::vector<std::string>& problems, const std::string& shown)
        {
            auto text = ReadFile(path);
            if (!text) { Problem(problems, shown, "cannot be read"); return json(); }
            json j = json::parse(*text, nullptr, false);
            if (j.is_discarded()) { Problem(problems, shown, "is not valid JSON"); return json(); }
            return j;
        }

        bool EndsWith(const std::string& s, const char* suffix)
        {
            const size_t n = std::char_traits<char>::length(suffix);
            return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
        }

        void ValidateActors(const json& actors, const std::string& where, std::vector<std::string>& problems, const std::string& file)
        {
            if (!actors.is_array()) { Problem(problems, file, where + " must be an array of actors"); return; }
            for (size_t i = 0; i < actors.size(); ++i) {
                const json& a = actors[i];
                const std::string here = where + "[" + std::to_string(i) + "]";
                if (!a.is_object()) { Problem(problems, file, here + " is not an object"); continue; }
                if (!a.contains("name") || !a["name"].is_string()) Problem(problems, file, here + " has no name");
                if (a.contains("components")) {
                    if (!a["components"].is_array()) Problem(problems, file, here + ".components must be an array");
                    else for (const json& c : a["components"])
                        if (!c.is_object() || !c.contains("type") || !c["type"].is_string()) Problem(problems, file, here + " has a component with no type");
                }
                if (a.contains("children")) ValidateActors(a["children"], here + ".children", problems, file);
            }
        }
    }

    std::vector<std::string> ValidateWorldJson(const json& w, const std::string& name)
    {
        std::vector<std::string> problems;
        if (!w.is_object()) { Problem(problems, name, "a world is a JSON object"); return problems; }
        if (!w.contains("name") || !w["name"].is_string()) Problem(problems, name, "a world needs a name");
        if (w.contains("actors")) ValidateActors(w["actors"], "actors", problems, name);
        if (w.contains("settings") && !w["settings"].is_object()) Problem(problems, name, "settings must be an object");
        return problems;
    }

    Core::Result<DeclaredComponent> ParseDeclaredComponent(const json& j)
    {
        if (!j.is_object()) return Core::Err("a component declaration is a JSON object");
        DeclaredComponent c;
        if (!j.contains("name") || !j["name"].is_string() || j["name"].get<std::string>().empty()) return Core::Err("a component needs a name");
        c.name = j["name"].get<std::string>();
        if (!j.contains("fields") || !j["fields"].is_object()) return Core::Err("component '" + c.name + "' needs a \"fields\" object");
        static const std::set<std::string> kTypes = { "bool", "int", "float", "string", "vec3", "color" };
        for (auto it = j["fields"].begin(); it != j["fields"].end(); ++it) {
            DeclaredField f;
            f.name = it.key();
            const json& d = it.value();
            if (!d.is_object() || !d.contains("type") || !d["type"].is_string()) return Core::Err("field '" + f.name + "' of '" + c.name + "' needs a type");
            f.type = d["type"].get<std::string>();
            if (!kTypes.count(f.type)) return Core::Err("field '" + f.name + "' of '" + c.name + "' has the unknown type '" + f.type + "'");
            if (d.contains("default")) {
                f.defaultValue = d["default"];
                const json& v = f.defaultValue;
                const bool ok = (f.type == "bool" && v.is_boolean()) || (f.type == "int" && v.is_number_integer()) || (f.type == "float" && v.is_number()) ||
                                (f.type == "string" && v.is_string()) || ((f.type == "vec3" || f.type == "color") && v.is_array() && (v.size() == 3 || (f.type == "color" && v.size() == 4)));
                if (!ok) return Core::Err("the default of '" + f.name + "' in '" + c.name + "' is not a " + f.type);
            } else {
                f.defaultValue = f.type == "bool" ? json(false) : f.type == "int" ? json(0) : f.type == "float" ? json(0.0) : f.type == "string" ? json("") :
                                 f.type == "vec3" ? json::array({ 0.0, 0.0, 0.0 }) : json::array({ 1.0, 1.0, 1.0, 1.0 });
            }
            c.fields.push_back(std::move(f));
        }
        return Core::Ok(std::move(c));
    }

    std::vector<std::string> ValidateContentFolder(const std::string& contentRoot)
    {
        std::vector<std::string> problems;
        std::error_code ec;
        if (!fs::is_directory(contentRoot, ec)) { problems.push_back("content folder '" + contentRoot + "' does not exist"); return problems; }

        std::vector<std::string> files;
        ListFiles(contentRoot, {}, files);
        std::sort(files.begin(), files.end());
        for (const std::string& f : files) {
            const std::string full = contentRoot + "/" + f;
            if (f.compare(0, 7, "worlds/") == 0 && EndsWith(f, ".world")) {
                json w = ReadJson(full, problems, f);
                if (w.is_null()) continue;
                if (!w.is_object()) { Problem(problems, f, "a world is a JSON object"); continue; }
                if (w.contains("actors")) ValidateActors(w["actors"], "actors", problems, f);
                if (w.contains("settings") && !w["settings"].is_object()) Problem(problems, f, "settings must be an object");
            } else if (f.compare(0, 8, "prefabs/") == 0 && EndsWith(f, ".json")) {
                json p = ReadJson(full, problems, f);
                if (p.is_null()) continue;
                if (!p.is_object() || !p.contains("name")) Problem(problems, f, "a prefab is an actor : an object with a name");
                else ValidateActors(json::array({ p }), "prefab", problems, f);
            } else if (f.compare(0, 7, "tables/") == 0 && EndsWith(f, ".json")) {
                json t = ReadJson(full, problems, f);
                if (!t.is_null() && !t.is_object()) Problem(problems, f, "a table is a JSON object");
            } else if (f.compare(0, 5, "lang/") == 0 && EndsWith(f, ".json")) {
                json t = ReadJson(full, problems, f);
                if (t.is_null()) continue;
                if (!t.is_object()) { Problem(problems, f, "a translation file is a JSON object"); continue; }
                std::vector<std::pair<std::string, const json*>> stack;
                for (auto it = t.begin(); it != t.end(); ++it) stack.push_back({ it.key(), &it.value() });
                while (!stack.empty()) {
                    auto [key, node] = stack.back();
                    stack.pop_back();
                    if (node->is_object()) { for (auto it = node->begin(); it != node->end(); ++it) stack.push_back({ key + "." + it.key(), &it.value() }); }
                    else if (!node->is_string()) Problem(problems, f, "'" + key + "' is not a string");
                }
            } else if (f.compare(0, 8, "patches/") == 0 && EndsWith(f, ".patch")) {
                json p = ReadJson(full, problems, f);
                if (p.is_null()) continue;
                if (!p.is_object() || !p.contains("world") || !p["world"].is_string()) { Problem(problems, f, "a patch needs the \"world\" it applies to"); continue; }
                if (!p.contains("ops") || !p["ops"].is_array()) { Problem(problems, f, "a patch needs an \"ops\" array"); continue; }
                // A trial run on an empty world catches the ops that are malformed, not the ones that name missing actors
                static const std::set<std::string> kOps = { "addActor", "removeActor", "setComponent", "addComponent", "removeComponent", "setSettings" };
                for (const json& op : p["ops"])
                    if (!op.is_object() || !op.contains("op") || !op["op"].is_string() || !kOps.count(op["op"].get<std::string>()))
                        Problem(problems, f, "unknown or missing op");
            } else if (f.compare(0, 11, "components/") == 0 && EndsWith(f, ".json")) {
                json c = ReadJson(full, problems, f);
                if (c.is_null()) continue;
                auto parsed = ParseDeclaredComponent(c);
                if (!parsed) Problem(problems, f, parsed.GetError().message);
            }
        }
        return problems;
    }

    // ---- World patches ----

    namespace {
        json* FindChild(json& actors, const std::string& name)
        {
            if (!actors.is_array()) return nullptr;
            for (json& a : actors)
                if (a.is_object() && a.value("name", std::string()) == name) return &a;
            return nullptr;
        }

        // The actor array that holds the actor at `path`'s parent, and the actor's name
        json* FindActor(json& world, const std::string& path)
        {
            if (!world.contains("actors")) return nullptr;
            std::stringstream ss(path);
            std::string part;
            json* list = &world["actors"];
            json* current = nullptr;
            while (std::getline(ss, part, '/')) {
                if (part.empty()) continue;
                current = FindChild(*list, part);
                if (!current) return nullptr;
                list = &(*current)["children"];
                if (!list->is_array()) *list = json::array();
            }
            return current;
        }

        json* ActorList(json& world, const std::string& parentPath)
        {
            if (parentPath.empty()) {
                if (!world.contains("actors")) world["actors"] = json::array();
                return &world["actors"];
            }
            json* parent = FindActor(world, parentPath);
            if (!parent) return nullptr;
            if (!parent->contains("children") || !(*parent)["children"].is_array()) (*parent)["children"] = json::array();
            return &(*parent)["children"];
        }

        json* FindComponent(json& actor, const std::string& type)
        {
            if (!actor.contains("components") || !actor["components"].is_array()) return nullptr;
            for (json& c : actor["components"])
                if (c.is_object() && c.value("type", std::string()) == type) return &c;
            return nullptr;
        }
    }

    Core::Result<bool> ApplyWorldPatch(json& world, const json& patch)
    {
        using namespace Core;
        if (!world.is_object()) return Err("the world is not a JSON object");
        if (!patch.is_object() || !patch.contains("ops") || !patch["ops"].is_array()) return Err("a patch needs an \"ops\" array");

        json work = world;          // applied to a copy : all or nothing
        size_t index = 0;
        for (const json& op : patch["ops"]) {
            const std::string where = "op " + std::to_string(index++) + " : ";
            if (!op.is_object() || !op.contains("op") || !op["op"].is_string()) return Err(where + "missing \"op\"");
            const std::string kind = op["op"].get<std::string>();
            const std::string path = op.value("path", std::string());

            if (kind == "addActor") {
                if (!op.contains("actor") || !op["actor"].is_object() || !op["actor"].contains("name")) return Err(where + "addActor needs an actor with a name");
                json* list = ActorList(work, op.value("parent", std::string()));
                if (!list) return Err(where + "parent '" + op.value("parent", std::string()) + "' not found");
                if (FindChild(*list, op["actor"]["name"].get<std::string>())) return Err(where + "an actor named '" + op["actor"]["name"].get<std::string>() + "' already exists there");
                list->push_back(op["actor"]);
            } else if (kind == "removeActor") {
                const size_t slash = path.rfind('/');
                json* list = ActorList(work, slash == std::string::npos ? std::string() : path.substr(0, slash));
                const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
                if (!list || !list->is_array()) return Err(where + "actor '" + path + "' not found");
                auto it = std::find_if(list->begin(), list->end(), [&](const json& a) { return a.is_object() && a.value("name", std::string()) == name; });
                if (it == list->end()) return Err(where + "actor '" + path + "' not found");
                list->erase(it);
            } else if (kind == "setComponent") {
                json* actor = FindActor(work, path);
                if (!actor) return Err(where + "actor '" + path + "' not found");
                if (!op.contains("type") || !op["type"].is_string() || !op.contains("fields") || !op["fields"].is_object()) return Err(where + "setComponent needs \"type\" and \"fields\"");
                json* comp = FindComponent(*actor, op["type"].get<std::string>());
                if (!comp) return Err(where + "'" + path + "' has no '" + op["type"].get<std::string>() + "' component");
                for (auto it = op["fields"].begin(); it != op["fields"].end(); ++it) (*comp)[it.key()] = it.value();
            } else if (kind == "addComponent") {
                json* actor = FindActor(work, path);
                if (!actor) return Err(where + "actor '" + path + "' not found");
                if (!op.contains("component") || !op["component"].is_object() || !op["component"].contains("type")) return Err(where + "addComponent needs a component with a type");
                if (!actor->contains("components") || !(*actor)["components"].is_array()) (*actor)["components"] = json::array();
                (*actor)["components"].push_back(op["component"]);
            } else if (kind == "removeComponent") {
                json* actor = FindActor(work, path);
                if (!actor || !actor->contains("components")) return Err(where + "actor '" + path + "' not found");
                if (!op.contains("type") || !op["type"].is_string()) return Err(where + "removeComponent needs a type");
                auto& comps = (*actor)["components"];
                auto it = std::find_if(comps.begin(), comps.end(), [&](const json& c) { return c.is_object() && c.value("type", std::string()) == op["type"].get<std::string>(); });
                if (it == comps.end()) return Err(where + "'" + path + "' has no '" + op["type"].get<std::string>() + "' component");
                comps.erase(it);
            } else if (kind == "setSettings") {
                if (!op.contains("settings") || !op["settings"].is_object()) return Err(where + "setSettings needs a \"settings\" object");
                if (!work.contains("settings") || !work["settings"].is_object()) work["settings"] = json::object();
                MergeInto(work["settings"], op["settings"]);
            } else {
                return Err(where + "unknown op '" + kind + "'");
            }
        }
        world = std::move(work);
        return Ok(true);
    }
}
