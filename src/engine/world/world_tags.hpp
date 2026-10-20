#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/core/tags/tag.hpp"

namespace Shard::Engine::Worlds {

    class World;

    /// @brief Replaces each tag of `world` by what `remap` gives for it (the empty tag : kept as it is) : the world's own tags, its
    /// actors', their components', and the reflected Tag / TagContainer / TagQuery fields of the components. What renaming a tag in
    /// the project does to the open world. Quiet : no TagsChangedEvent. Returns how many objects changed.
    size_t RemapTags(World& world, const std::function<Core::Tag(Core::Tag)>& remap);

    /// @brief Where `tag` (or a tag under it) is used in `world`, one line each : "World", "Door_01", "Door_01 > PhysicsBody" (the class),
    /// "Door_01 > DamageZone.affects" (a reflected field).
    void CollectTagReferences(World& world, Core::Tag tag, std::vector<std::string>& out);

    /// @brief The same, read from a world file (its JSON) : the "tags" of the world, of the actors and of the components, and the
    /// strings of the components that name the tag. Without the components themselves a file does not say which of its strings are
    /// tags or queries : any string of a component that reads as a tag or a query naming this one is listed, which may list more than there is.
    void CollectTagReferences(const nlohmann::json& worldData, const std::string& tagName, std::vector<std::string>& out);
}
