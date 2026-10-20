#include "world_tags.hpp"

#include <algorithm>

#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/core/tags/tag_container.hpp"
#include "engine/core/tags/tag_query.hpp"
#include "engine/core/tags/tag_registry.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/world.hpp"

namespace Shard::Engine::Worlds {

    namespace {
        /// Every actor of the world, the children included, parents first
        std::vector<std::shared_ptr<Objects::Actor>> AllActors(World& world)
        {
            std::vector<std::shared_ptr<Objects::Actor>> out;
            std::vector<std::shared_ptr<Objects::Actor>> pending;
            for (auto& [id, actor] : world.GetRootActors())
                pending.push_back(actor);
            std::sort(pending.begin(), pending.end(), [](const auto& a, const auto& b) { return b->GetID() < a->GetID(); });

            while (!pending.empty()) {
                std::shared_ptr<Objects::Actor> actor = pending.back();
                pending.pop_back();
                if (!actor)
                    continue;
                out.push_back(actor);
                std::vector<Core::ObjectID> children = actor->GetChildrenID(false);
                for (auto it = children.rbegin(); it != children.rend(); ++it)
                    if (auto child = std::dynamic_pointer_cast<Objects::Actor>(actor->GetChild(*it)))
                        pending.push_back(child);
            }
            return out;
        }

        bool Remap(Core::TagContainer& tags, const std::function<Core::Tag(Core::Tag)>& remap)
        {
            Core::TagContainer mapped;
            for (Core::Tag tag : tags) {
                const Core::Tag to = remap(tag);
                mapped.Add(to.IsValid() ? to : tag);
            }
            if (mapped == tags)
                return false;
            tags.Assign(mapped, true);
            return true;
        }

        bool Uses(const Core::TagContainer& tags, Core::Tag tag)
        {
            for (Core::Tag t : tags)
                if (t.Matches(tag))
                    return true;
            return false;
        }

        std::string ComponentLabel(const Objects::Components::Component& component)
        {
            const ClassDescriptor* descriptor = component.GetDescriptor();
            return descriptor ? descriptor->name : std::string("component");
        }
    }

    size_t RemapTags(World& world, const std::function<Core::Tag(Core::Tag)>& remap)
    {
        size_t changed = Remap(world.GetTags(), remap) ? 1 : 0;

        for (auto& actor : AllActors(world)) {
            if (Remap(actor->GetTags(), remap))
                ++changed;

            for (auto& component : actor->GetComponents()) {
                bool componentChanged = Remap(component->GetTags(), remap);

                const ClassDescriptor* descriptor = component->GetDescriptor();
                if (!descriptor) {
                    changed += componentChanged ? 1 : 0;
                    continue;
                }

                for (const FieldInfo* field : descriptor->fields) {
                    void* value = FieldRead(*field, component.get());
                    bool fieldChanged = false;

                    if (field->type == TypeID::Tag) {
                        Core::Tag& tag = *static_cast<Core::Tag*>(value);
                        const Core::Tag to = tag.IsValid() ? remap(tag) : Core::Tag();
                        if (to.IsValid() && to != tag) {
                            tag = to;
                            fieldChanged = true;
                        }
                    }
                    else if (field->type == TypeID::TagContainer)
                        fieldChanged = Remap(*static_cast<Core::TagContainer*>(value), remap);
                    else if (field->type == TypeID::TagQuery) {
                        Core::TagQuery& query = *static_cast<Core::TagQuery*>(value);
                        Core::TagQuery mapped = query.Remapped(remap);
                        if (mapped != query) {
                            query = std::move(mapped);
                            fieldChanged = true;
                        }
                    }

                    if (fieldChanged) {
                        component->OnFieldChanged(FieldChangedEvent{ field });
                        componentChanged = true;
                    }
                }

                changed += componentChanged ? 1 : 0;
            }
        }

        return changed;
    }

    void CollectTagReferences(World& world, Core::Tag tag, std::vector<std::string>& out)
    {
        if (!tag.IsValid())
            return;

        if (Uses(world.GetTags(), tag))
            out.push_back("World");

        for (auto& actor : AllActors(world)) {
            if (Uses(actor->GetTags(), tag))
                out.push_back(actor->GetName());

            for (auto& component : actor->GetComponents()) {
                const std::string label = actor->GetName() + " > " + ComponentLabel(*component);
                if (Uses(component->GetTags(), tag))
                    out.push_back(label);

                const ClassDescriptor* descriptor = component->GetDescriptor();
                if (!descriptor)
                    continue;

                for (const FieldInfo* field : descriptor->fields) {
                    const void* value = FieldRead(*field, component.get());
                    bool uses = false;
                    if (field->type == TypeID::Tag)
                        uses = static_cast<const Core::Tag*>(value)->Matches(tag);
                    else if (field->type == TypeID::TagContainer)
                        uses = Uses(*static_cast<const Core::TagContainer*>(value), tag);
                    else if (field->type == TypeID::TagQuery)
                        for (Core::Tag t : static_cast<const Core::TagQuery*>(value)->GetTags())
                            uses = uses || t.Matches(tag);

                    if (uses)
                        out.push_back(label + "." + field->name);
                }
            }
        }
    }

    namespace {
        /// `name` is the tag or a tag under it
        bool NamesTag(const std::string& name, const std::string& tagName)
        {
            return name == tagName || (name.size() > tagName.size() && name.compare(0, tagName.size(), tagName) == 0 && name[tagName.size()] == '.');
        }

        bool UsesInArray(const nlohmann::json& tags, const std::string& tagName)
        {
            if (!tags.is_array())
                return false;
            for (const nlohmann::json& name : tags)
                if (name.is_string() && NamesTag(name.get<std::string>(), tagName))
                    return true;
            return false;
        }

        /// A string that is a tag or a query (it parses as one) and names the tag
        bool MentionsTag(const std::string& text, const std::string& tagName)
        {
            std::string token;
            bool mentions = false;
            auto flush = [&] {
                if (!token.empty() && NamesTag(token, tagName))
                    mentions = true;
                token.clear();
            };
            for (char c : text) {
                const bool nameChar = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
                if (nameChar) {
                    token += c;
                    continue;
                }
                if (c != ' ' && c != '&' && c != '|' && c != '!' && c != '=' && c != '(' && c != ')')
                    return false;
                flush();
            }
            flush();
            if (!mentions)
                return false;

            // Names side by side ("Enemy camp") are a sentence, not a query. Parsed without keeping anything : no name is reported
            Core::TagQuery query;
            return Core::TagQuery::TryParse(text, query, nullptr, false);
        }

        void ScanActor(const nlohmann::json& actor, const std::string& tagName, std::vector<std::string>& out)
        {
            if (!actor.is_object())
                return;

            const std::string name = actor.value("name", std::string("?"));
            if (actor.contains("tags") && UsesInArray(actor["tags"], tagName))
                out.push_back(name);

            if (actor.contains("components") && actor["components"].is_array()) {
                for (const nlohmann::json& component : actor["components"]) {
                    if (!component.is_object())
                        continue;
                    const std::string label = name + " > " + component.value("type", std::string("component"));
                    if (component.contains("tags") && UsesInArray(component["tags"], tagName))
                        out.push_back(label);

                    for (auto& [key, value] : component.items()) {
                        if (key == "type" || key == "active" || key == "tags")
                            continue;
                        bool uses = value.is_string() && MentionsTag(value.get<std::string>(), tagName);
                        if (value.is_array())
                            for (const nlohmann::json& element : value)
                                uses = uses || (element.is_string() && MentionsTag(element.get<std::string>(), tagName));
                        if (uses)
                            out.push_back(label + "." + key);
                    }
                }
            }

            if (actor.contains("children") && actor["children"].is_array())
                for (const nlohmann::json& child : actor["children"])
                    ScanActor(child, tagName, out);
        }
    }

    void CollectTagReferences(const nlohmann::json& worldData, const std::string& tagName, std::vector<std::string>& out)
    {
        if (!worldData.is_object() || tagName.empty())
            return;

        if (worldData.contains("tags") && UsesInArray(worldData["tags"], tagName))
            out.push_back("World");

        if (worldData.contains("actors") && worldData["actors"].is_array())
            for (const nlohmann::json& actor : worldData["actors"])
                ScanActor(actor, tagName, out);
    }
}
