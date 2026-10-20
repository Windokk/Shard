#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "engine/core/tags/tag.hpp"
#include "engine/core/tags/tag_container.hpp"
#include "engine/core/tags/tag_query.hpp"
#include "engine/core/tags/tag_registry.hpp"

namespace Shard::Engine::Serialization {

    /// The names of the tags, sorted : the "tags" array of a world, an actor or a component
    inline nlohmann::ordered_json TagsToJson(const Core::TagContainer& tags)
    {
        nlohmann::ordered_json array = nlohmann::ordered_json::array();
        for (const std::string& name : tags.ToStrings())
            array.push_back(name);
        return array;
    }

    /// @brief What TagsToJson wrote. The names are resolved like any file's (redirects followed, an unknown tag kept and reported) ;
    /// what is not a string is skipped.
    inline Core::TagContainer TagsFromJson(const nlohmann::json& array)
    {
        Core::TagContainer tags;
        if (!array.is_array())
            return tags;
        for (const nlohmann::json& name : array)
            if (name.is_string())
                tags.Add(Core::TagRegistry::Get().Resolve(name.get<std::string>()));
        return tags;
    }
}
