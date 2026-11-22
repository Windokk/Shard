#include "prefab.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "engine/assets/assetID.hpp"
#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/world/engine.hpp"

namespace Shard::Engine::Worlds {

    using json = nlohmann::json;

    namespace {
        constexpr int kMaxDepth = 16;

        PrefabContentReader& ContentReader()
        {
            static PrefabContentReader reader;
            return reader;
        }

        bool EndsWith(const std::string& s, const std::string& suffix)
        {
            return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        bool ReadProjectAsset(const std::string& candidate, std::string& text, std::string& canonical)
        {
            Core::IEngineContext* engine = &Core::GetEngine();
            Filesystem::AssetIDManager* assets = engine ? engine->GetAssetIDManager() : nullptr;
            if (!assets)
                return false;
            auto info = assets->GetAssetFromID(assets->GetIDFromNameInProject(candidate));
            if (!info || info->baseInfos.nameInProject != candidate)
                return false;
            text = info->baseInfos.path.ReadFile();
            canonical = candidate;
            return true;
        }

        PrefabContentReader& ProjectReader()
        {
            static PrefabContentReader reader = &ReadProjectAsset;
            return reader;
        }

        bool ReadPrefabText(const std::string& name, std::string& text, std::string& canonical)
        {
            if (name.empty())
                return false;

            if (name.compare(0, 6, "mod://") != 0) {
                std::vector<std::string> candidates;
                candidates.push_back(name);
                if (!EndsWith(name, ".prefab")) {
                    candidates.push_back(name + ".prefab");
                    if (name.find('/') == std::string::npos)
                        candidates.push_back("prefabs/" + name + ".prefab");
                }
                for (const std::string& candidate : candidates)
                    if (ProjectReader()(candidate, text, canonical))
                        return true;
            }

            if (ContentReader() && ContentReader()(name, text, canonical))
                return true;
            return false;
        }

        bool NumbersEqual(const json& a, const json& b)
        {
            const double x = a.get<double>();
            const double y = b.get<double>();
            if (x == y)
                return true;
            return std::fabs(x - y) <= 1e-5 * std::max({ 1.0, std::fabs(x), std::fabs(y) });
        }

        bool Equal(const json& a, const json& b)
        {
            if (a.is_number() && b.is_number())
                return NumbersEqual(a, b);
            if (a.type() != b.type())
                return false;
            if (a.is_object()) {
                if (a.size() != b.size())
                    return false;
                for (auto it = a.begin(); it != a.end(); ++it) {
                    auto other = b.find(it.key());
                    if (other == b.end() || !Equal(it.value(), *other))
                        return false;
                }
                return true;
            }
            if (a.is_array()) {
                if (a.size() != b.size())
                    return false;
                for (size_t i = 0; i < a.size(); ++i)
                    if (!Equal(a[i], b[i]))
                        return false;
                return true;
            }
            return a == b;
        }

        json MergePatchDiff(const json& a, const json& b)
        {
            if (a.is_object() && b.is_object()) {
                json patch = json::object();
                for (auto it = b.begin(); it != b.end(); ++it) {
                    auto other = a.find(it.key());
                    if (other == a.end())
                        patch[it.key()] = it.value();
                    else if (!Equal(*other, it.value()))
                        patch[it.key()] = MergePatchDiff(*other, it.value());
                }
                for (auto it = a.begin(); it != a.end(); ++it)
                    if (!b.contains(it.key()))
                        patch[it.key()] = nullptr;
                return patch;
            }
            return b;
        }

        std::vector<std::string> KeysOf(const json& list, const char* field)
        {
            std::vector<std::string> keys;
            std::unordered_map<std::string, int> seen;
            if (!list.is_array())
                return keys;
            for (const json& item : list) {
                const std::string base = item.is_object() && item.contains(field) && item[field].is_string() ? item[field].get<std::string>() : std::string();
                const int n = seen[base]++;
                keys.push_back(n == 0 ? base : base + "#" + std::to_string(n));
            }
            return keys;
        }

        int IndexOfKey(const std::vector<std::string>& keys, const std::string& key)
        {
            for (size_t i = 0; i < keys.size(); ++i)
                if (keys[i] == key)
                    return int(i);
            return -1;
        }

        void EraseIndices(json& list, std::vector<int> indices)
        {
            std::sort(indices.begin(), indices.end(), std::greater<int>());
            indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
            for (int i : indices)
                list.erase(list.begin() + i);
        }
    }

    void SetPrefabContentReader(PrefabContentReader reader)
    {
        ContentReader() = std::move(reader);
    }

    void SetPrefabProjectReader(PrefabContentReader reader)
    {
        ProjectReader() = reader ? std::move(reader) : PrefabContentReader(&ReadProjectAsset);
    }

    PrefabContentReader GetPrefabProjectReader()
    {
        return ProjectReader();
    }

    PrefabContentReader GetPrefabContentReader()
    {
        return ContentReader();
    }

    std::string PrefabStem(const std::string& name)
    {
        std::string stem = name;
        const size_t slash = stem.find_last_of('/');
        if (slash != std::string::npos)
            stem = stem.substr(slash + 1);
        for (const char* ext : { ".prefab", ".json" })
            if (EndsWith(stem, ext))
                stem.resize(stem.size() - std::string(ext).size());
        return stem;
    }

    bool IsPrefabLink(const json& node)
    {
        return node.is_object() && node.contains("prefab") && node["prefab"].is_string() && !node.contains("components");
    }

    bool LoadResolvedPrefab(const std::string& name, json& out, std::string& canonical, int depth)
    {
        if (depth > kMaxDepth) {
            DEBUG_ERROR("Prefab " + name + " is nested too deep (a prefab that contains itself ?)");
            return false;
        }

        std::string text;
        if (!ReadPrefabText(name, text, canonical))
            return false;

        json data = json::parse(text, nullptr, false);
        if (data.is_discarded() || !data.is_object() || !data.contains("name"))
            return false;

        out = ExpandPrefabLinks(data, depth + 1);
        out["prefab"] = canonical;
        out.erase("prefabUnresolved");
        return true;
    }

    json ExpandPrefabLinks(const json& node, int depth)
    {
        if (!node.is_object())
            return node;

        if (IsPrefabLink(node)) {
            json base;
            std::string canonical;
            const std::string target = node["prefab"].get<std::string>();
            if (!LoadResolvedPrefab(target, base, canonical, depth)) {
                DEBUG_WARNING("Prefab " + target + " was not found (kept as it is in the file)");
                json placeholder = json::object();
                placeholder["name"] = node.value("name", std::string("Actor"));
                placeholder["prefab"] = target;
                placeholder["prefabUnresolved"] = node;
                placeholder["components"] = json::array();
                return placeholder;
            }
            if (node.contains("name") && node["name"].is_string())
                base["name"] = node["name"];
            if (node.contains("overrides"))
                ApplyPrefabDiff(base, node["overrides"]);
            return base;
        }

        json out = node;
        if (out.contains("children") && out["children"].is_array())
            for (json& child : out["children"])
                child = ExpandPrefabLinks(child, depth);
        return out;
    }

    json DiffPrefabActor(const json& base, const json& current)
    {
        json diff = json::object();

        for (const char* field : { "folder", "tags" }) {
            const json b = base.contains(field) ? base[field] : json();
            const json c = current.contains(field) ? current[field] : json();
            if (!Equal(b, c))
                diff[field] = c;
        }

        const json noList = json::array();
        const json& baseComponents = base.contains("components") && base["components"].is_array() ? base["components"] : noList;
        const json& currentComponents = current.contains("components") && current["components"].is_array() ? current["components"] : noList;
        const auto baseKeys = KeysOf(baseComponents, "type");
        const auto currentKeys = KeysOf(currentComponents, "type");

        for (size_t i = 0; i < currentKeys.size(); ++i) {
            const int at = IndexOfKey(baseKeys, currentKeys[i]);
            if (at < 0) {
                diff["addComponents"].push_back(currentComponents[i]);
                continue;
            }
            json patch = MergePatchDiff(baseComponents[at], currentComponents[i]);
            if (!patch.empty())
                diff["components"][currentKeys[i]] = std::move(patch);
        }
        for (const std::string& key : baseKeys)
            if (IndexOfKey(currentKeys, key) < 0)
                diff["removeComponents"].push_back(key);

        const json& baseChildren = base.contains("children") && base["children"].is_array() ? base["children"] : noList;
        const json& currentChildren = current.contains("children") && current["children"].is_array() ? current["children"] : noList;
        const auto baseChildKeys = KeysOf(baseChildren, "name");
        const auto currentChildKeys = KeysOf(currentChildren, "name");

        for (size_t i = 0; i < currentChildKeys.size(); ++i) {
            const int at = IndexOfKey(baseChildKeys, currentChildKeys[i]);
            if (at < 0) {
                diff["addChildren"].push_back(currentChildren[i]);
                continue;
            }
            json sub = DiffPrefabActor(baseChildren[at], currentChildren[i]);
            if (!sub.empty())
                diff["children"][currentChildKeys[i]] = std::move(sub);
        }
        for (const std::string& key : baseChildKeys)
            if (IndexOfKey(currentChildKeys, key) < 0)
                diff["removeChildren"].push_back(key);

        return diff;
    }

    void ApplyPrefabDiff(json& actor, const json& diff)
    {
        if (!actor.is_object() || !diff.is_object())
            return;

        for (const char* field : { "folder", "tags" }) {
            if (!diff.contains(field))
                continue;
            if (diff[field].is_null())
                actor.erase(field);
            else
                actor[field] = diff[field];
        }

        if (!actor.contains("components") || !actor["components"].is_array())
            actor["components"] = json::array();
        const auto componentKeys = KeysOf(actor["components"], "type");

        if (diff.contains("components") && diff["components"].is_object())
            for (auto it = diff["components"].begin(); it != diff["components"].end(); ++it) {
                const int at = IndexOfKey(componentKeys, it.key());
                if (at >= 0)
                    actor["components"][at].merge_patch(it.value());
            }

        if (diff.contains("removeComponents") && diff["removeComponents"].is_array()) {
            std::vector<int> erase;
            for (const json& key : diff["removeComponents"])
                if (key.is_string()) {
                    const int at = IndexOfKey(componentKeys, key.get<std::string>());
                    if (at >= 0)
                        erase.push_back(at);
                }
            EraseIndices(actor["components"], std::move(erase));
        }

        if (diff.contains("addComponents") && diff["addComponents"].is_array())
            for (const json& component : diff["addComponents"])
                actor["components"].push_back(component);

        if (!actor.contains("children") || !actor["children"].is_array())
            actor["children"] = json::array();
        const auto childKeys = KeysOf(actor["children"], "name");

        if (diff.contains("children") && diff["children"].is_object())
            for (auto it = diff["children"].begin(); it != diff["children"].end(); ++it) {
                const int at = IndexOfKey(childKeys, it.key());
                if (at >= 0)
                    ApplyPrefabDiff(actor["children"][at], it.value());
            }

        if (diff.contains("removeChildren") && diff["removeChildren"].is_array()) {
            std::vector<int> erase;
            for (const json& key : diff["removeChildren"])
                if (key.is_string()) {
                    const int at = IndexOfKey(childKeys, key.get<std::string>());
                    if (at >= 0)
                        erase.push_back(at);
                }
            EraseIndices(actor["children"], std::move(erase));
        }

        if (diff.contains("addChildren") && diff["addChildren"].is_array())
            for (const json& child : diff["addChildren"])
                actor["children"].push_back(child);

        if (actor["children"].empty())
            actor.erase("children");
    }

    json MakeTransformOverride(const float position[3], const float rotationQuat[4], const float scale[3])
    {
        const glm::quat q = glm::normalize(glm::quat(rotationQuat[3], rotationQuat[0], rotationQuat[1], rotationQuat[2]));
        const glm::vec3 euler = glm::degrees(glm::eulerAngles(q));

        json transform = json::object();
        transform["position"] = { { "x", position[0] }, { "y", position[1] }, { "z", position[2] } };
        transform["rotation"] = { { "x", euler.x }, { "y", euler.y }, { "z", euler.z } };
        transform["scale"] = { { "x", scale[0] }, { "y", scale[1] }, { "z", scale[2] } };

        json overrides = json::object();
        overrides["components"]["transform"] = std::move(transform);
        return overrides;
    }
}
