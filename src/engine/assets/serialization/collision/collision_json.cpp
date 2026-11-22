#include "collision_json.hpp"

#include "engine/assets/serialization/tags/tag_json.hpp"
#include "engine/assets/vfs/filesystem.hpp"

#include <algorithm>

namespace Shard::Engine::Serialization {

    namespace {
        using nlohmann::json;
        using nlohmann::ordered_json;

        void Report(std::vector<std::string>* problems, const std::string& text)
        {
            if (problems)
                problems->push_back(text);
        }

        std::string StringOf(const json& value)
        {
            if (value.is_string())
                return value.get<std::string>();
            if (value.is_boolean())
                return value.get<bool>() ? "true" : "false";
            if (value.is_number())
                return value.dump();
            return std::string();
        }

        template<class Enum>
        bool ReadEnum(const json& data, const char* key, Enum& out, const std::string& owner, std::vector<std::string>* problems)
        {
            if (!data.contains(key))
                return false;
            if (!data[key].is_string() || !Collision::TryParse(data[key].get<std::string>(), out)) {
                Report(problems, owner + " : invalid value for '" + key + "'");
                return false;
            }
            return true;
        }

        float ReadFloat(const json& data, const char* key, float fallback)
        {
            return data.contains(key) && data[key].is_number() ? data[key].get<float>() : fallback;
        }
    }

    Collision::Profile ProfileFromJson(const std::string& name, const json& data, std::vector<std::string>* problems)
    {
        Collision::Profile profile;
        profile.name = name;
        if (!data.is_object()) {
            Report(problems, "profile '" + name + "' must be an object");
            return profile;
        }
        const std::string owner = "profile '" + name + "'";
        if (data.contains("base") && data["base"].is_string())
            profile.base = data["base"].get<std::string>();
        if (data.contains("identity"))
            profile.identity = TagsFromJson(data["identity"]);
        if (data.contains("rules") && data["rules"].is_array()) {
            for (const json& entry : data["rules"]) {
                if (!entry.is_object() || !entry.contains("target") || !entry["target"].is_string()) {
                    Report(problems, owner + " : a rule needs a 'target' and a 'response'");
                    continue;
                }
                Collision::Rule rule;
                std::string error;
                if (!Core::TagQuery::TryParse(entry["target"].get<std::string>(), rule.target, &error))
                    Report(problems, owner + " : target '" + entry["target"].get<std::string>() + "' : " + error);
                if (!entry.contains("response") || !entry["response"].is_string() || !Collision::TryParse(entry["response"].get<std::string>(), rule.response)) {
                    Report(problems, owner + " : invalid response in a rule");
                    continue;
                }
                profile.rules.push_back(std::move(rule));
            }
        }
        Collision::Response response;
        if (ReadEnum(data, "default", response, owner, problems))
            profile.defaultResponse = response;
        Collision::Mode mode;
        if (ReadEnum(data, "mode", mode, owner, problems))
            profile.mode = mode;
        if (data.contains("material") && data["material"].is_string())
            profile.material = data["material"].get<std::string>();
        if (data.contains("events") && data["events"].is_object()) {
            const json& events = data["events"];
            Collision::EventOptions options;
            if (events.contains("overlaps") && events["overlaps"].is_boolean())
                options.overlaps = events["overlaps"].get<bool>();
            if (events.contains("hits") && events["hits"].is_boolean())
                options.hits = events["hits"].get<bool>();
            options.minImpactSpeed = ReadFloat(events, "minImpactSpeed", 0.0f);
            profile.events = options;
        }
        return profile;
    }

    ordered_json ProfileToJson(const Collision::Profile& profile)
    {
        ordered_json out = ordered_json::object();
        if (!profile.base.empty())
            out["base"] = profile.base;
        if (!profile.identity.IsEmpty())
            out["identity"] = TagsToJson(profile.identity);
        if (!profile.rules.empty()) {
            ordered_json rules = ordered_json::array();
            for (const Collision::Rule& rule : profile.rules) {
                ordered_json entry = ordered_json::object();
                entry["target"] = rule.target.ToString();
                entry["response"] = Collision::ToString(rule.response);
                rules.push_back(entry);
            }
            out["rules"] = rules;
        }
        if (profile.defaultResponse)
            out["default"] = Collision::ToString(*profile.defaultResponse);
        if (profile.mode)
            out["mode"] = Collision::ToString(*profile.mode);
        if (!profile.material.empty())
            out["material"] = profile.material;
        if (profile.events) {
            ordered_json events = ordered_json::object();
            events["overlaps"] = profile.events->overlaps;
            events["hits"] = profile.events->hits;
            events["minImpactSpeed"] = profile.events->minImpactSpeed;
            out["events"] = events;
        }
        return out;
    }

    Collision::PhysicsMaterial MaterialFromJson(const std::string& name, const json& data, std::vector<std::string>* problems)
    {
        Collision::PhysicsMaterial material;
        material.name = name;
        if (!data.is_object()) {
            Report(problems, "material '" + name + "' must be an object");
            return material;
        }
        material.friction = ReadFloat(data, "friction", material.friction);
        material.restitution = ReadFloat(data, "restitution", material.restitution);
        material.density = ReadFloat(data, "density", material.density);
        if (data.contains("combine")) {
            if (!data["combine"].is_string() || !Collision::TryParse(data["combine"].get<std::string>(), material.combine))
                Report(problems, "material '" + name + "' : invalid 'combine'");
        }
        if (data.contains("surface") && data["surface"].is_string())
            material.surface = data["surface"].get<std::string>();
        return material;
    }

    ordered_json MaterialToJson(const Collision::PhysicsMaterial& material)
    {
        ordered_json out = ordered_json::object();
        out["friction"] = material.friction;
        out["restitution"] = material.restitution;
        out["density"] = material.density;
        out["combine"] = Collision::ToString(material.combine);
        if (!material.surface.empty())
            out["surface"] = material.surface;
        return out;
    }

    std::vector<Collision::PhysicsMaterial> LoadPhysicsMaterialAssets(const Filesystem::AssetIDManager& ids, std::vector<std::string>* problems)
    {
        std::vector<Collision::PhysicsMaterial> out;
        for (const auto& [id, info] : ids.AssetIDMap) {
            if (id.GetAsInt() < Filesystem::kFirstProjectAssetID || !info || info->baseInfos.type != Filesystem::Type::T_PHYSICS_MATERIAL)
                continue;
            const std::string& name = info->baseInfos.nameInProject;
            const std::string text = info->baseInfos.path.ReadFile();
            json data = json::parse(text, nullptr, false);
            if (data.is_discarded()) {
                Report(problems, "physics material '" + name + "' is not valid JSON");
                continue;
            }
            out.push_back(MaterialFromJson(name, data, problems));
        }
        std::sort(out.begin(), out.end(), [](const Collision::PhysicsMaterial& a, const Collision::PhysicsMaterial& b) { return a.name < b.name; });
        return out;
    }

    Collision::InteractionRow InteractionFromJson(const json& data, std::vector<std::string>* problems)
    {
        Collision::InteractionRow row;
        if (!data.is_object()) {
            Report(problems, "an interaction row must be an object");
            return row;
        }
        if (data.contains("name") && data["name"].is_string())
            row.name = data["name"].get<std::string>();
        const std::string owner = row.name.empty() ? "interaction row" : "interaction row '" + row.name + "'";
        for (const char* key : { "a", "b" }) {
            Core::TagQuery& query = key[0] == 'a' ? row.a : row.b;
            if (!data.contains(key))
                continue;
            std::string error;
            if (!data[key].is_string() || !Core::TagQuery::TryParse(data[key].get<std::string>(), query, &error))
                Report(problems, owner + " : '" + key + "' : " + error);
        }
        ReadEnum(data, "event", row.event, owner, problems);
        row.minSpeed = ReadFloat(data, "minSpeed", 0.0f);
        if (data.contains("actions") && data["actions"].is_array()) {
            for (const json& entry : data["actions"]) {
                if (!entry.is_object() || !entry.contains("type") || !entry["type"].is_string()) {
                    Report(problems, owner + " : an action needs a 'type'");
                    continue;
                }
                Collision::Action action;
                action.type = entry["type"].get<std::string>();
                for (auto it = entry.begin(); it != entry.end(); ++it)
                    if (it.key() != "type")
                        action.params.emplace_back(it.key(), StringOf(it.value()));
                row.actions.push_back(std::move(action));
            }
        }
        return row;
    }

    ordered_json InteractionToJson(const Collision::InteractionRow& row)
    {
        ordered_json out = ordered_json::object();
        if (!row.name.empty())
            out["name"] = row.name;
        out["a"] = row.a.ToString();
        out["b"] = row.b.ToString();
        out["event"] = Collision::ToString(row.event);
        if (row.minSpeed != 0.0f)
            out["minSpeed"] = row.minSpeed;
        ordered_json actions = ordered_json::array();
        for (const Collision::Action& action : row.actions) {
            ordered_json entry = ordered_json::object();
            entry["type"] = action.type;
            for (const auto& [key, value] : action.params)
                entry[key] = value;
            actions.push_back(entry);
        }
        out["actions"] = actions;
        return out;
    }
}
