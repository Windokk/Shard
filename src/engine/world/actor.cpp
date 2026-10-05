#include "actor.hpp"

#include <algorithm>

#include "engine/world/engine.hpp"

#include "engine/assets/project/project.hpp"
#include "engine/audio/audio_source.hpp"

namespace Shard::Engine::Objects{
    
    using namespace Components;

    Actor::Actor(std::string name, Core::IEngineContext* engine) : engine(engine)
    {
        SetName(name);
    }

    void Actor::Init(){
        this->transform = AddComponent<Transform>();
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

        components.push_back(component);

        // Register in system component arrays
        if (auto light = std::dynamic_pointer_cast<Light>(component)) {
            light->SetLightIndex(level->lights.size());
            level->lights.push_back(light);
            level->lightComps.emplace(GetComponentIDInLevel(light->GetLocalId()), light);
        }

        if (auto model = std::dynamic_pointer_cast<Model>(component)) {
            level->models.emplace(GetComponentIDInLevel(model->GetLocalId()), model);
        }

        if (auto transform = std::dynamic_pointer_cast<Transform>(component)) {
            level->transforms.emplace(GetComponentIDInLevel(transform->GetLocalId()), transform);
        }

        if (auto physics = std::dynamic_pointer_cast<PhysicsBody>(component)) {
            level->physicsBodies.emplace(GetComponentIDInLevel(physics->GetLocalId()), physics);
        }

        if (auto audio = std::dynamic_pointer_cast<AudioSource>(component)) {
            level->audioSources.emplace(GetComponentIDInLevel(audio->GetLocalId()), audio);
        }

        if (auto script = std::dynamic_pointer_cast<Script>(component)) {
            level->scripts.emplace(GetComponentIDInLevel(script->GetLocalId()), script);

            uint32_t id = GetComponentIDInLevel(components.size() - 1);

            RegisterComponentEvents(script);

            script->OnCreate();
        }

        if (auto cam = std::dynamic_pointer_cast<Camera>(component)) {
            level->cameras.emplace(GetComponentIDInLevel(cam->GetLocalId()), cam);
            engine->GetCameraManager()->AddCamera(GetID(), cam);
        }

        return component;
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

        if (level)
            level->RemoveComponent(GetComponentIDInLevel(component->GetLocalId()), component);
        else
            component->Destroy();

        components.erase(it);
    }

    void Actor::Destroy()
    {
        for(auto& component : components){

            if(level)
                level->RemoveComponent(GetComponentIDInLevel(component->GetLocalId()), component);
            else
                component->Destroy();
        }

        if(level){

            level->RemoveActor(id);
            
            if(level->IsLoaded()){
                int levelBuildIndex = level->GetBuildIndex();
                int levelAssetID = engine->GetAssetIDManager()->GetIDFromNameInProject(engine->GetBuildSettings()->buildIndex[levelBuildIndex].full).GetAsInt();

                engine->GetEventDispatcher()->emitGlobal(Events::LevelStructureChangedEvent(
                                                        levelAssetID, Events::DESTROYED, name, GetID()));
            }
        }
        
        LevelObject::Destroy();
    }

    void Actor::AddChild(std::shared_ptr<LevelObject> o)
    {
        LevelObject::AddChild(o);
        if (std::shared_ptr<Actor> actorChild = std::dynamic_pointer_cast<Actor>(o)) {
            actorChild->SetLevel(this->level);

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
            std::shared_ptr<LevelObject> ancestor = newParent;
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
        else if (level)
            level->RemoveActor(id);

        if (newParent) {
            newParent->AddChild(AsShared<Actor>());
        }
        else {
            LevelObject::SetParent(Core::ObjectID(-1));
            if (level)
                level->AddActor(AsShared<Actor>());
        }

        if (keepWorldTransform)
            transform->SetFromWorldMatrix(worldMatrix);
        else
            transform->MarkWorldMatrixDirty();
    }

    void Actor::SetLevel(Levels::Level* lvl)
    {
        if(!lvl) return;

        this->level = lvl;

        level->transforms.emplace(GetComponentIDInLevel(transform->GetLocalId()), transform);

        if(level->IsLoaded()){
            int levelBuildIndex = level->GetBuildIndex();
            int levelAssetID = engine->GetAssetIDManager()->GetIDFromNameInProject(engine->GetBuildSettings()->buildIndex[levelBuildIndex].full).GetAsInt();

            engine->GetEventDispatcher()->emitGlobal(Events::LevelStructureChangedEvent(
                                                    levelAssetID, Events::CREATED, name, GetID()));
        }
    }

    void Actor::RegisterComponentEvents(const std::shared_ptr<Script>& component){
        
        engine->GetEventDispatcher()->subscribeToComponent<Events::ContactAddedEvent>(GetComponentIDInLevel(components.size()-1), [component](const Events::ContactAddedEvent& event) {
            component->OnContactAdded(event);
        });
        engine->GetEventDispatcher()->subscribeToComponent<Events::ContactPersistedEvent>(GetComponentIDInLevel(components.size()-1), [component](const Events::ContactPersistedEvent& event) {
            component->OnContactPersisted(event);
        });
        engine->GetEventDispatcher()->subscribeToComponent<Events::ContactRemovedEvent>(GetComponentIDInLevel(components.size()-1), [component](const Events::ContactRemovedEvent& event) {
            component->OnContactEnded(event);
        });
    }

    void Actor::Activate()
    {
        activated = true;
        for(auto& component : components){
            component->Activate();
        }
        if(level->IsLoaded()){
            int levelBuildIndex = level->GetBuildIndex();
            int levelAssetID = engine->GetAssetIDManager()->GetIDFromNameInProject(engine->GetBuildSettings()->buildIndex[levelBuildIndex].full).GetAsInt();

            engine->GetEventDispatcher()->emitGlobal(Events::LevelStructureChangedEvent(
                                                    levelAssetID, Events::ACTIVATED, name, GetID()));
        }
    }

    void Actor::DeActivate()
    {
        activated = false;
        for(auto& component : components){
            component->DeActivate();
        }
        if(level->IsLoaded()){
            int levelBuildIndex = level->GetBuildIndex();
            int levelAssetID = engine->GetAssetIDManager()->GetIDFromNameInProject(engine->GetBuildSettings()->buildIndex[levelBuildIndex].full).GetAsInt();

            engine->GetEventDispatcher()->emitGlobal(Events::LevelStructureChangedEvent(
                                                    levelAssetID, Events::DEACTIVATED, name, GetID()));
        }
    }
    
    std::shared_ptr<Actor> Actor::Clone()
    {
        std::shared_ptr<Actor> copy = Core::Object::CreateWithContext<Actor>(engine, "Copy of "+name, engine);

        if(level)
            level->transforms.erase(GetComponentIDInLevel(copy->transform->GetLocalId()));
        
        copy->transform->Destroy();

        copy->components.clear();

        DEBUG_INFO("Cloning actor : "+name);

        if(GetParent() && std::dynamic_pointer_cast<Actor>(GetParent())){
            std::shared_ptr<Actor> p = std::dynamic_pointer_cast<Actor>(GetParent());
            GetParent()->AddChild(copy);
        }
        else if(level){
            level->AddActor(copy);
        }
        else{
            DEBUG_INFO("Cloning : Base actor was not placed in a level, clone won't be placed in a level either");
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

                if(copy->level && copy->level->IsLoaded())
                    level->transforms.emplace(copy->GetComponentIDInLevel(tr->GetLocalId()), tr);

                copy->transform = tr;
            }
            
            if(!copy->level || !copy->level->IsLoaded())
                continue;

            if(std::shared_ptr<Components::AudioSource> audioSource = std::dynamic_pointer_cast<Components::AudioSource>(cloneComp)){
                level->audioSources.emplace(copy->GetComponentIDInLevel(audioSource->GetLocalId()), audioSource);
                audioSource->Update();
            }
            else if(std::shared_ptr<Components::Script> script = std::dynamic_pointer_cast<Components::Script>(cloneComp)){    
                level->scripts.emplace(copy->GetComponentIDInLevel(script->GetLocalId()), script);
                RegisterComponentEvents(script);
                script->OnCreate();
            }
            else if(std::shared_ptr<Components::Camera> camera = std::dynamic_pointer_cast<Components::Camera>(cloneComp)){
                level->cameras.emplace(copy->GetComponentIDInLevel(camera->GetLocalId()), camera);
                engine->GetCameraManager()->AddCamera(copy->GetID(), camera);
            }
            else if(std::shared_ptr<Components::Light> light = std::dynamic_pointer_cast<Components::Light>(cloneComp)){
                light->SetLightIndex(level->lights.size());
                level->lights.push_back(light);
                level->lightComps.emplace(copy->GetComponentIDInLevel(light->GetLocalId()), light);
            }
            else if(std::shared_ptr<Components::Model> model = std::dynamic_pointer_cast<Components::Model>(cloneComp))
            {
                level->models.emplace(copy->GetComponentIDInLevel(model->GetLocalId()), model);
                model->Update();
            }
            else if(std::shared_ptr<Components::PhysicsBody> physicsBody = std::dynamic_pointer_cast<Components::PhysicsBody>(cloneComp)){
                level->physicsBodies.emplace(copy->GetComponentIDInLevel(physicsBody->GetLocalId()), physicsBody);
            }
        }

        return copy;
    }
}