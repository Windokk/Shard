#include "component.hpp"

#include "engine/core/diagnostics/logger.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/event_system.hpp"
#include "engine/world/world.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/assets/serialization/reflection/reflection_serializer.hpp"

namespace Shard::Engine::Objects::Components {

    Component::Component(std::shared_ptr<Actor> parent, uint32_t local_id)
    {
        this->parent = parent;
        this->local_id = local_id;
        tags.SetListener(this);
    }

    Component::Component(const Component& other)
        : Core::Object(other), Core::TagContainer::Listener(), parent(other.parent), local_id(other.local_id), activated(other.activated), tags(other.tags)
    {
        tags.SetListener(this);
    }

    Component& Component::operator=(const Component& other)
    {
        if (this == &other)
            return *this;
        Core::Object::operator=(other);
        parent = other.parent;
        local_id = other.local_id;
        activated = other.activated;
        tags.Assign(other.tags, true);
        return *this;
    }

    void Component::OnTagsChanged(const Core::TagContainer& added, const Core::TagContainer& removed, bool quiet)
    {
        Worlds::World* world = parent ? parent->world : nullptr;
        if (!world)
            return;

        const int idInWorld = parent->GetComponentIDInWorld(local_id);
        // Not owned yet (being built) : nothing to index, it is indexed when it joins the world
        if (auto self = std::static_pointer_cast<Component>(weak_from_this().lock()))
            world->IndexComponentTags(idInWorld, self, added, removed);

        if (quiet || !world->IsLoaded())
            return;
        if (Core::IEngineContext* engine = GetEngineContext())
            if (Events::EventDispatcher* events = engine->GetEventDispatcher())
                events->emitToComponent(Events::EventDispatcher::ComponentID(idInWorld), Events::TagsChangedEvent(added, removed, GetID()));
    }

    Component::~Component()
    {
    }

    Core::IEngineContext* Component::GetEngineContext() const
    {
        return parent ? parent->GetEngineContext() : nullptr;
    }

    namespace
    {
        constexpr const char* kTypeKey = "type";
        constexpr const char* kActiveKey = "active";
        constexpr const char* kTagsKey = "tags";        // written by the world, for every component (see SerializeActor)

        /// Only the assets need the engine (their names live in its asset database)
        Serialization::ReflectionContext MakeContext(const ClassDescriptor& descriptor, Core::IEngineContext* engine)
        {
            for (const FieldInfo* field : descriptor.fields)
            {
                const bool isAsset = field->type == TypeID::Asset || (field->container && field->container->elementType == TypeID::Asset);
                if (isAsset && engine)
                    return Serialization::ReflectionContext{ engine->GetAssetIDManager() };
            }
            return Serialization::ReflectionContext{};
        }
    }

    void Component::Deserialize(const json componentData)
    {
        DeserializeReflected(componentData);
    }

    ordered_json Component::Serialize()
    {
        const ClassDescriptor* descriptor = GetDescriptor();
        return SerializeReflected(descriptor ? descriptor->name : "");
    }

    ordered_json Component::SerializeReflected(const std::string& typeName) const
    {
        ordered_json comp;

        comp[kTypeKey] = typeName;
        comp[kActiveKey] = activated;

        const ClassDescriptor* descriptor = GetDescriptor();
        if (!descriptor)
            return comp;

        const Serialization::ReflectionContext context = MakeContext(*descriptor, GetEngineContext());

        ordered_json fields;
        Serialization::WriteFields(descriptor->fields, this, fields, context);

        for (auto& [key, value] : fields.items())
        {
            if (key == kTypeKey || key == kActiveKey || key == kTagsKey)
            {
                DEBUG_WARNING("Field '" + key + "' of " + typeName + " is not serialized : the name is reserved");
                continue;
            }
            comp[key] = std::move(value);
        }

        return comp;
    }

    void Component::DeserializeReflected(const json& componentData, bool defaultActive)
    {
        DeserializeReflectedFields(componentData);
        DeserializeActive(componentData, defaultActive);
    }

    void Component::DeserializeReflectedFields(const json& componentData, bool notify)
    {
        const ClassDescriptor* descriptor = GetDescriptor();
        if (!descriptor)
            return;

        const Serialization::ReflectionContext context = MakeContext(*descriptor, GetEngineContext());

        Serialization::ReadFields(descriptor->fields, this, componentData, context, [this, notify](const FieldInfo& field) {
            if (notify)
                OnFieldChanged(FieldChangedEvent{ &field });
        });
    }

    void Component::DeserializeActive(const json& componentData, bool defaultActive)
    {
        bool active = defaultActive;
        if (componentData.contains(kActiveKey) && componentData[kActiveKey].is_boolean())
            active = componentData[kActiveKey].get<bool>();

        if (active)
            Activate();
        else
            DeActivate();
    }
}