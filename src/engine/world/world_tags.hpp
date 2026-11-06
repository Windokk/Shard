#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/core/tags/tag.hpp"

namespace Shard::Engine::Worlds {

    class World;

    size_t RemapTags(World& world, const std::function<Core::Tag(Core::Tag)>& remap);

    void CollectTagReferences(World& world, Core::Tag tag, std::vector<std::string>& out);

    void CollectTagReferences(const nlohmann::json& worldData, const std::string& tagName, std::vector<std::string>& out);
}
