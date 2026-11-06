#pragma once

#include "engine/core/ecs/entity.hpp"
#include "engine/world/world_object.hpp"

#include "engine/world/engine.hpp"

#include "engine/world/components/transform.hpp"
#include "engine/world/components/script.hpp"

#include "engine/world/world.hpp"

#include "engine/world/event_system.hpp"

#include <stdexcept>
#include <iostream>
#include <vector>

#include <nlohmann/json.hpp>

namespace Shard::Engine::Objects{

    using namespace Components;

    class Actor : public WorldObject, public Core::TagContainer::Listener{
        
        std::vector<std::shared_ptr<Component>> components;
        std::string name;
        std::string folder;
        Core::TagContainer tags;
        // Components of an unregistered type (plugin not loaded) are kept and written back, so saving without the plugin loses nothing.
        std::vector<nlohmann::json> unknownComponents;
        Core::IEngineContext* engine = nullptr;
        Core::Ecs::Entity entity;
        public:
            Actor(std::string name, Core::IEngineContext* engine);

            Core::IEngineContext* GetEngineContext() const { return engine; }

            Core::Ecs::Entity GetEntity() const { return entity; }

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

            const std::vector<nlohmann::json>& GetUnknownComponents() const { return unknownComponents; }
            void AddUnknownComponent(nlohmann::json serialized) { unknownComponents.push_back(std::move(serialized)); }

            template <typename T>
            std::shared_ptr<T> AddComponent();

            std::shared_ptr<Component> AddComponentByName(const std::string& typeName);

            void Destroy() override;

            int GetComponentIDInWorld(int componentIndex) { 
                if(componentIndex >= 0 && id.GetAsInt() >= 0){
                    return (id.GetAsInt() << 12) | (componentIndex & 0xFFF);
                }
                DEBUG_ERROR("Either local component ID or actor id is not greater than 0. Returning default value (0).");
                return 0;
            }
            
            std::string GetName() { return name; }
            void SetName(std::string name) { this->name = name; }

            const std::string& GetFolder() const { return folder; }
            void SetFolder(std::string path) { folder = std::move(path); }

            Core::TagContainer& GetTags() { return tags; }
            const Core::TagContainer& GetTags() const { return tags; }

            void OnTagsChanged(const Core::TagContainer& added, const Core::TagContainer& removed, bool quiet) override;

            void AddChild(std::shared_ptr<WorldObject> o) override;

            void SetWorld(Worlds::World* world);

            std::shared_ptr<Transform> transform = nullptr;
            Worlds::World* world = nullptr;

            void Activate();

            void DeActivate();

            bool IsActive(){
                return activated;
            }

            std::shared_ptr<Actor> Clone();

            void SetParent(std::shared_ptr<Actor> newParent, bool keepWorldTransform = true);

        protected:
            bool activated = true;

        private:
            void AttachComponent(const std::shared_ptr<Component>& component);

            void RegisterInWorld(const std::shared_ptr<Component>& component, bool cloned);
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

        AttachComponent(component);

        return component;
    }

}