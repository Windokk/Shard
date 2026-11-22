#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/assets/assetID.hpp"
#include "engine/core/collision/collision_profile.hpp"
#include "engine/core/collision/interaction_table.hpp"

namespace Shard::Engine::Serialization {

    Collision::Profile ProfileFromJson(const std::string& name, const nlohmann::json& data, std::vector<std::string>* problems = nullptr);
    nlohmann::ordered_json ProfileToJson(const Collision::Profile& profile);

    Collision::PhysicsMaterial MaterialFromJson(const std::string& name, const nlohmann::json& data, std::vector<std::string>* problems = nullptr);
    nlohmann::ordered_json MaterialToJson(const Collision::PhysicsMaterial& material);

    std::vector<Collision::PhysicsMaterial> LoadPhysicsMaterialAssets(const Filesystem::AssetIDManager& ids, std::vector<std::string>* problems = nullptr);

    Collision::InteractionRow InteractionFromJson(const nlohmann::json& data, std::vector<std::string>* problems = nullptr);
    nlohmann::ordered_json InteractionToJson(const Collision::InteractionRow& row);
}
