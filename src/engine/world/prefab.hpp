#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace Shard::Engine::Worlds {

    using PrefabContentReader = std::function<bool(const std::string& name, std::string& text, std::string& canonical)>;

    void SetPrefabContentReader(PrefabContentReader reader);
    PrefabContentReader GetPrefabContentReader();

    void SetPrefabProjectReader(PrefabContentReader reader);
    PrefabContentReader GetPrefabProjectReader();

    std::string PrefabStem(const std::string& name);

    bool IsPrefabLink(const nlohmann::json& node);

    bool LoadResolvedPrefab(const std::string& name, nlohmann::json& out, std::string& canonical, int depth = 0);

    nlohmann::json ExpandPrefabLinks(const nlohmann::json& node, int depth = 0);

    nlohmann::json DiffPrefabActor(const nlohmann::json& base, const nlohmann::json& current);

    void ApplyPrefabDiff(nlohmann::json& actor, const nlohmann::json& diff);

    nlohmann::json MakeTransformOverride(const float position[3], const float rotationQuat[4], const float scale[3]);
}
