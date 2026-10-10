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
    }

    void Actor::Init(){
        // The entity that mirrors this actor : hierarchy now, data that systems work on as it moves out of the components
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
        // Set actor and component index
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

        if (world != nullptr)
            RegisterInWorld(component, false);
    }

    void Actor::RegisterInWorld(const std::shared_ptr<Component>& component, bool cloned)
    {
        const int idInWorld = GetComponentIDInWorld(component->GetLocalId());

        if (auto tr = std::dynamic_pointer_cast<Transform>(component))
            world->transforms.emplace(idInWorld, tr);

        // The other modules index what they own (lights, models, physics bodies...)
        world->NotifyComponentAdded(idInWorld, component, cloned);

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
            
            if(world->IsLoaded()){
                int worldBuildIndex = world->GetBuildIndex();
                int worldAssetID = engine->GetAssetIDManager()->GetIDFromNameInProject(engine->GetBuildSettings()->buildIndex[worldBuildIndex].full).GetAsInt();

                engine->GetEventDispatcher()->emitGlobal(Events::WorldStructureChangedEvent(
                                                        worldAssetID, Events::DESTROYED, name, GetID()));
            }
        }
        
        WorldObject::Destroy();

        // The children went first (each released its own entity) ; this takes whatever is left of the subtree with it
        // (or as soon as the iteration that is running is over : a system may destroy actors from inside a query)
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

            // The child's cached world matrix (if any) was computed against its old parent chain
            // (or none at all) and must be invalidated now that it hangs off this actor instead.
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

        // Refuse a reparent that would create a cycle (dropping an actor onto itself or one of its
        // own descendants).
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

        if(world->IsLoaded()){
            int worldBuildIndex = world->GetBuildIndex();
            int worldAssetID = engine->GetAssetIDManager()->GetIDFromNameInProject(engine->GetBuildSettings()->buildIndex[worldBuildIndex].full).GetAsInt();

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
            int worldBuildIndex = world->GetBuildIndex();
            int worldAssetID = engine->GetAssetIDManager()->GetIDFromNameInProject(engine->GetBuildSettings()->buildIndex[worldBuildIndex].full).GetAsInt();

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
            int worldBuildIndex = world->GetBuildIndex();
            int worldAssetID = engine->GetAssetIDManager()->GetIDFromNameInProject(engine->GetBuildSettings()->buildIndex[worldBuildIndex].full).GetAsInt();

            engine->GetEventDispatcher()->emitGlobal(Events::WorldStructureChangedEvent(
                                                    worldAssetID, Events::DEACTIVATED, name, GetID()));
        }
    }
    
    std::shared_ptr<Actor> Actor::Clone()
    {
        std::shared_ptr<Actor> copy = Core::Object::CreateWithContext<Actor>(engine, "Copy of "+name, engine);

        if(world)
            world->transforms.erase(GetComponentIDInWorld(copy->transform->GetLocalId()));
        
        copy->transform->Destroy();

        copy->components.clear();

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

        for(int i = 0; i < components.size(); i++){

            std::shared_ptr<Component> comp = components[i];

            std::shared_ptr<Component> cloneComp = comp->Clone();

            cloneComp->SetParent(copy);

            copy->components.push_back(cloneComp);

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