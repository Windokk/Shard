#pragma once

#include "engine/world/level_object.hpp"

#include "engine/world/engine.hpp"

#include "engine/world/components/transform.hpp"
#include "engine/renderer/components/light_component.hpp"
#include "engine/renderer/components/probe_volume.hpp"
#include "engine/physics/physics_body.hpp"

#include "engine/renderer/frontend/camera_manager.hpp"

#include "engine/world/levels/level.hpp"

#include "engine/world/event_system.hpp"

#include <stdexcept>
#include <iostream>

namespace Shard::Engine::Objects{

    using namespace Components;

    class Actor : public LevelObject{
        
        std::vector<std::shared_ptr<Component>> components;
        std::string name;
        Core::IEngineContext* engine = nullptr;
        public:
            Actor(std::string name, Core::IEngineContext* engine);

            Core::IEngineContext* GetEngineContext() const { return engine; }

            void Init();

            std::shared_ptr<Component> AddComponentRaw(std::shared_ptr<Component> component);

            void RemoveComponent(std::shared_ptr<Component> component);

            template <typename T>
            bool HasComponent();

            template <typename T>
            std::vector<std::shared_ptr<T>> GetComponents();

            template <typename T>
            std::shared_ptr<T> GetComponent(int k = 0);

            const std::vector<std::shared_ptr<Component>>& GetComponents() const {
                return components;
            }

            template <typename T>
            std::shared_ptr<T> AddComponent();

            void Destroy() override;

            int GetComponentIDInLevel(int componentIndex) { 
                if(componentIndex >= 0 && id.GetAsInt() >= 0){
                    return (id.GetAsInt() << 12) | (componentIndex & 0xFFF);
                }
                DEBUG_ERROR("Either local component ID or actor id is not greater than 0. Returning default value (0).");
                return 0;
            }
            
            std::string GetName() { return name; }
            void SetName(std::string name) { this->name = name; }

            void AddChild(std::shared_ptr<LevelObject> o) override;

            void SetLevel(Levels::Level* lvl);

            void RegisterComponentEvents(const std::shared_ptr<Script>& component);

            std::shared_ptr<Transform> transform = nullptr;
            Levels::Level* level = nullptr;

            void Activate();

            void DeActivate();

            bool IsActive(){
                return activated;
            }

            std::shared_ptr<Actor> Clone();

            /// @brief Attaches this actor to newParent (or detaches to the level root if nullptr).
            /// @param keepWorldTransform If true (default), the actor's local Transform is recomputed
            /// so its world-space position/rotation/scale stay the same after reparenting - like
            /// "attach in place" in Unity/Unreal. If false, the local Transform is left untouched, so
            /// the actor jumps to be positioned relative to its new parent instead.
            void SetParent(std::shared_ptr<Actor> newParent, bool keepWorldTransform = true);

        protected:
            bool activated = true;
    };

    

    template <typename T>
    bool Actor::HasComponent() {
        if(!std::is_base_of<Component, T>::value){
            DEBUG_ERROR("T must inherit from Component");
        }

        for (const std::shared_ptr<Component>& component : components) {
            if (dynamic_cast<T*>(component.get())) {
                return true;
            }
        }
        return false;
    }

    template <typename T>
    std::vector<std::shared_ptr<T>> Actor::GetComponents(){
        if(!std::is_base_of<Component, T>::value){
            DEBUG_ERROR("T must inherit from Component");
        }

        std::vector<std::shared_ptr<T>> list;

        for (const auto& component : components) {
            if (auto casted = std::dynamic_pointer_cast<T>(component)) {
                list.push_back(casted);
            }
        }

        return list;
    } 

    template <typename T>
    std::shared_ptr<T> Actor::GetComponent(int k) {
        if(!std::is_base_of<Component, T>::value){
            DEBUG_ERROR("T must inherit from Component");
        }

        int n = 0;

        for (auto& component : components) {
            if (auto casted = std::dynamic_pointer_cast<T>(component)) {
                if(n != k){
                    n++;
                    continue;
                }
                else{
                    return casted;
                }
            }
        }
        DEBUG_ERROR(std::string("Failed to retrieve Component of type ") + typeid(T).name());
        return nullptr;
    }

    
    template <typename T>
    std::shared_ptr<T> Actor::AddComponent()
    {
        if(!IsSubclassOf<Component, T>()){
            DEBUG_ERROR("T must inherit from Component");
            return nullptr;
        }

        if (transform != nullptr && IsSubclassOf<Transform, T>()) {
            DEBUG_ERROR("An actor can only have one transform component.");
            return nullptr;
        }

        std::shared_ptr<T> component = Object::CreateWithContext<T>(engine, AsShared<Actor>(), components.size());

        components.push_back(component);

        if(level != nullptr){
            if constexpr (IsSubclassOf<Light, T>()) {
                component->SetLightIndex(level->lights.size());
                level->lights.push_back(component);
                level->lightComps.emplace(GetComponentIDInLevel(component->GetLocalId()), component);
            }

            if constexpr (IsSubclassOf<Model, T>()) {
                level->models.emplace(GetComponentIDInLevel(component->GetLocalId()), component);
            }

            if constexpr (IsSubclassOf<Transform, T>()) {
                level->transforms.emplace(GetComponentIDInLevel(component->GetLocalId()), component);
            }

            if constexpr (IsSubclassOf<PhysicsBody, T>()) {
                level->physicsBodies.emplace(GetComponentIDInLevel(component->GetLocalId()), component);
            }

            if constexpr (IsSubclassOf<ProbeVolume, T>()) {
                level->probeVolume = component;
                component->Activate();
            }

            if constexpr (IsSubclassOf<AudioSource, T>()) {
                level->audioSources.emplace(GetComponentIDInLevel(component->GetLocalId()), component);
            }

            if constexpr (IsSubclassOf<Script, T>()) {
                level->scripts.emplace(GetComponentIDInLevel(component->GetLocalId()), component);
                RegisterComponentEvents(component);
                component->OnCreate();
            }

            if constexpr (IsSubclassOf<Camera, T>()) {
                level->cameras.emplace(GetComponentIDInLevel(component->GetLocalId()), component);
                static_pointer_cast<Camera>(component)->AddToCameraManager();
            }
        }

        return component;
    }

}