#include "actor.hpp"

#include <algorithm>

#include "engine/world/engine.hpp"

#include "engine/assets/project/project.hpp"
#include "engine/core/ecs/registry.hpp"
#include "engine/world/components/registry/component_registry.hpp"
#include "engine/world/ecs_components.hpp"

namespace Shard::Engine::Objects{

    using namespace Components;

    namespace {
        Core::Ecs::Registry* EcsOf(Core::IEngineContext* engine)
        {
            return engine ? engine->GetEcs() : nullptr;
        }
    }

    Actor::Actor(std::string name, Core::IEngineContext* engine) : engine(engine)
    {
        SetName(name);
        tags.SetListener(this);
    }

    void Actor::OnTagsChanged(const Core::TagContainer& added, const Core::TagContainer& removed, bool quiet)
    {
        if (!world)
            return;

        world->IndexActorTags(AsShared<Actor>(), added, removed);

        if (!quiet && world->IsLoaded() && engine)
            engine->GetEventDispatcher()->emitToActor(id, Events::TagsChangedEvent(added, removed, id));
    }

    void Actor::Init(){
        if(Core::Ecs::Registry* ecs = EcsOf(engine))
            entity = ecs->Create(Worlds::ActorLink{id.GetAsInt()});

        this->transform = AddComponent<Transform>();
        if(transform)
            transform->SyncEntity();
    }

    std::shared_ptr<Component> Actor::AddComponentRaw(std::shared_ptr<Component> component) {
        if (!component) {
            DEBUG_ERROR("Tried to add null component.");
        }
        component->SetParent(std::static_pointer_cast<Actor>(shared_from_this()));
        component->SetLocalId(components.size());

        if (dynamic_cast<Transform*>(component.get())) {
            DEBUG_ERROR("An actor can only have one transform component.");
        }

        AttachComponent(component);

        return component;
    }

    std::shared_ptr<Component> Actor::AddComponentByName(const std::string& typeName)
    {
        auto& registry = GetComponentRegistry();

        if (registry.IsBuiltinComponent(typeName)) {
            std::shared_ptr<Component> component = registry.CreateBuiltinComponent(typeName, engine, AsShared<Actor>(), components.size());
            AttachComponent(component);
            return component;
        }

        std::shared_ptr<Component> raw = registry.CreateComponentByName(typeName);
        if (!raw)
            return nullptr;

        return AddComponentRaw(raw);
    }

    void Actor::AttachComponent(const std::shared_ptr<Component>& component)
    {
        components.push_back(component);
        component->OnAttached();

        if (world != nullptr)
            RegisterInWorld(component, false);
    }

    void Actor::RegisterInWorld(const std::shared_ptr<Component>& component, bool cloned)
    {
        const int idInWorld = GetComponentIDInWorld(component->GetLocalId());

        if (auto tr = std::dynamic_pointer_cast<Transform>(component))
            world->transforms.emplace(idInWorld, tr);

        world->NotifyComponentAdded(idInWorld, component, cloned);
        world->IndexComponentTags(idInWorld, component, component->GetTags(), Core::TagContainer());

        if (auto script = std::dynamic_pointer_cast<Script>(component)) {
            world->scripts.emplace(idInWorld, script);
            script->OnCreate();
        }
    }

    void Actor::RemoveComponent(std::shared_ptr<Component> component)
    {
        if (!component) {
            DEBUG_ERROR("Tried to remove null component.");
            return;
        }

        if (dynamic_cast<Transform*>(component.get())) {
            DEBUG_ERROR("The Transform component cannot be removed from an actor.");
            return;
        }

        auto it = std::find(components.begin(), components.end(), component);
        if (it == components.end()) {
            DEBUG_ERROR("Tried to remove a component that does not belong to this actor.");
            return;
        }

        if (world)
            world->RemoveComponent(GetComponentIDInWorld(component->GetLocalId()), component);
        else
            component->Destroy();

        components.erase(it);
    }

    bool Actor::MoveComponent(size_t from, size_t to)
    {
        if (from >= components.size() || to >= components.size() || from == to)
            return false;

        // Index 0 is the Transform, it has to stay there (the file is read back with it first)
        if (dynamic_cast<Transform*>(components[from].get()) || dynamic_cast<Transform*>(components[to].get()))
            return false;

        std::shared_ptr<Component> moved = components[from];
        components.erase(components.begin() + from);
        components.insert(components.begin() + to, std::move(moved));
        return true;
    }

    bool Actor::MoveComponent(const std::shared_ptr<Component>& component, size_t to)
    {
        auto it = std::find(components.begin(), components.end(), component);
        if (it == components.end())
            return false;

        return MoveComponent(static_cast<size_t>(it - components.begin()), to);
    }

    void Actor::SetComponentsOrder(const std::vector<std::shared_ptr<Component>>& order)
    {
        std::vector<std::shared_ptr<Component>> result;
        result.reserve(components.size());

        // The Transform first, then the requested ones, then whatever was not mentioned
        for (const auto& component : components)
            if (dynamic_cast<Transform*>(component.get()))
                result.push_back(component);

        auto has = [&](const std::shared_ptr<Component>& c){ return std::find(result.begin(), result.end(), c) != result.end(); };
        auto owned = [&](const std::shared_ptr<Component>& c){ return std::find(components.begin(), components.end(), c) != components.end(); };

        for (const auto& component : order)
            if (component && owned(component) && !has(component))
                result.push_back(component);

        for (const auto& component : components)
            if (!has(component))
                result.push_back(component);

        components = std::move(result);
    }

    bool Actor::MoveNextTo(const std::shared_ptr<Actor>& anchor, bool after)
    {
        if (!anchor || anchor.get() == this)
            return false;

        std::shared_ptr<WorldObject> parentObject = GetParent();
        std::shared_ptr<WorldObject> anchorParent = anchor->GetParent();

        if (parentObject) {
            if (!anchorParent || anchorParent->GetID() != parentObject->GetID())
                return false;
            return parentObject->MoveChildNextTo(id, anchor->GetID(), after);
        }

        if (anchorParent || !world || anchor->world != world)
            return false;

        return world->MoveRootActorNextTo(id, anchor->GetID(), after);
    }

    void Actor::Destroy()
    {
        for(auto& component : components){

            if(world)
                world->RemoveComponent(GetComponentIDInWorld(component->GetLocalId()), component);
            else
                component->Destroy();
        }

        if(world){

            world->RemoveActor(id);
            world->IndexActorTags(AsShared<Actor>(), Core::TagContainer(), tags);
            if(prefab)
                world->IndexPrefab(AsShared<Actor>(), false);
            
            if(world->IsLoaded()){
                int worldAssetID = world->ResolveAssetID();

                engine->GetEventDispatcher()->emitGlobal(Events::WorldStructureChangedEvent(
                                                        worldAssetID, Events::DESTROYED, name, GetID()));
            }
        }
        
        WorldObject::Destroy();

        for(auto& component : components)
            component->parent = nullptr;

        if(Core::Ecs::Registry* ecs = EcsOf(engine))
            ecs->DestroyDeferred(entity);
        entity = Core::Ecs::kNullEntity;
    }

    void Actor::AddChild(std::shared_ptr<WorldObject> o)
    {
        WorldObject::AddChild(o);
        if (std::shared_ptr<Actor> actorChild = std::dynamic_pointer_cast<Actor>(o)) {
            if (Core::Ecs::Registry* ecs = EcsOf(engine))
                ecs->SetParent(actorChild->entity, entity);

            actorChild->SetWorld(this->world);

            if (actorChild->transform)
                actorChild->transform->MarkWorldMatrixDirty();
        }
    }

    void Actor::SetParent(std::shared_ptr<Actor> newParent, bool keepWorldTransform)
    {
        if (newParent.get() == this) {
            DEBUG_ERROR("An actor cannot be parented to itself.");
            return;
        }

        if (newParent) {
            std::shared_ptr<WorldObject> ancestor = newParent;
            while (ancestor) {
                if (ancestor->GetID() == id) {
                    DEBUG_ERROR("Cannot parent an actor to one of its own descendants.");
                    return;
                }
                ancestor = ancestor->GetParent();
            }
        }

        std::shared_ptr<Actor> oldParent = std::dynamic_pointer_cast<Actor>(GetParent());

        if (oldParent.get() == newParent.get())
            return;

        glm::mat4 worldMatrix = transform->GetWorldMatrix();

        if (oldParent)
            oldParent->DeleteChildRef(id);
        else if (world)
            world->RemoveActor(id);

        if (newParent) {
            newParent->AddChild(AsShared<Actor>());
        }
        else {
            WorldObject::SetParent(Core::ObjectID(-1));
            if (Core::Ecs::Registry* ecs = EcsOf(engine))
                ecs->SetParent(entity, Core::Ecs::kNullEntity);
            if (world)
                world->AddActor(AsShared<Actor>());
        }

        if (keepWorldTransform)
            transform->SetFromWorldMatrix(worldMatrix);
        else
            transform->MarkWorldMatrixDirty();
    }

    void Actor::SetWorld(Worlds::World* world)
    {
        if(!world) return;

        this->world = world;

        world->transforms.emplace(GetComponentIDInWorld(transform->GetLocalId()), transform);
        world->IndexActorTags(AsShared<Actor>(), tags, Core::TagContainer());
        if(prefab)
            world->IndexPrefab(AsShared<Actor>(), true);

        if(world->IsLoaded()){
            int worldAssetID = world->ResolveAssetID();

            engine->GetEventDispatcher()->emitGlobal(Events::WorldStructureChangedEvent(
                                                    worldAssetID, Events::CREATED, name, GetID()));
        }
    }

    void Actor::Activate()
    {
        activated = true;
        if(Core::Ecs::Registry* ecs = EcsOf(engine))
            ecs->Remove<Worlds::Disabled>(entity);
        for(auto& component : components){
            component->Activate();
        }
        if(world->IsLoaded()){
            int worldAssetID = world->ResolveAssetID();

            engine->GetEventDispatcher()->emitGlobal(Events::WorldStructureChangedEvent(
                                                    worldAssetID, Events::ACTIVATED, name, GetID()));
        }
    }

    void Actor::DeActivate()
    {
        activated = false;
        if(Core::Ecs::Registry* ecs = EcsOf(engine))
            ecs->Add(entity, Worlds::Disabled{});
        for(auto& component : components){
            component->DeActivate();
        }
        if(world->IsLoaded()){
            int worldAssetID = world->ResolveAssetID();

            engine->GetEventDispatcher()->emitGlobal(Events::WorldStructureChangedEvent(
                                                    worldAssetID, Events::DEACTIVATED, name, GetID()));
        }
    }
    
    void Actor::SetPrefab(std::string prefabName, nlohmann::json unresolvedLink)
    {
        if(prefab && world)
            world->IndexPrefab(AsShared<Actor>(), false);
        prefab = PrefabInfo{ std::move(prefabName), std::move(unresolvedLink) };
        if(world)
            world->IndexPrefab(AsShared<Actor>(), true);
    }

    void Actor::ClearPrefab()
    {
        if(!prefab)
            return;
        if(world)
            world->IndexPrefab(AsShared<Actor>(), false);
        prefab.reset();
    }

    std::shared_ptr<Actor> Actor::GetPrefabRoot()
    {
        std::shared_ptr<Actor> current = AsShared<Actor>();
        while(current){
            if(current->prefab)
                return current;
            current = std::dynamic_pointer_cast<Actor>(current->GetParent());
        }
        return nullptr;
    }

    std::shared_ptr<Actor> Actor::Clone()
    {
        std::shared_ptr<Actor> copy = Core::Object::CreateWithContext<Actor>(engine, "Copy of "+name, engine);

        if(world)
            world->transforms.erase(GetComponentIDInWorld(copy->transform->GetLocalId()));
        
        copy->transform->Destroy();

        copy->components.clear();
        copy->unknownComponents = unknownComponents;
        copy->tags.Assign(tags, true);

        DEBUG_INFO("Cloning actor : "+name);

        if(GetParent() && std::dynamic_pointer_cast<Actor>(GetParent())){
            std::shared_ptr<Actor> p = std::dynamic_pointer_cast<Actor>(GetParent());
            GetParent()->AddChild(copy);
        }
        else if(world){
            world->AddActor(copy);
        }
        else{
            DEBUG_INFO("Cloning : Base actor was not placed in a world, clone won't be placed in a world either");
        }

        if(prefab && prefab->unresolved.is_null())
            copy->SetPrefab(prefab->name);

        for(size_t i = 0; i < components.size(); i++){

            std::shared_ptr<Component> comp = components[i];

            std::shared_ptr<Component> cloneComp = comp->Clone();

            cloneComp->SetParent(copy);
            cloneComp->GetTags().Assign(comp->GetTags(), true);

            copy->components.push_back(cloneComp);
            cloneComp->OnAttached();

            if (cloneComp->IsInstanceOf<Transform>()) {
                
                std::shared_ptr<Transform> tr = std::dynamic_pointer_cast<Components::Transform>(cloneComp);

                tr->SetPosition(transform->GetPosition());
                tr->SetRotation(transform->GetRotation());
                tr->SetScale(transform->GetScale());

                copy->transform = tr;
            }
            
            if(!copy->world || !copy->world->IsLoaded())
                continue;

            copy->RegisterInWorld(cloneComp, true);
        }

        return copy;
    }
}