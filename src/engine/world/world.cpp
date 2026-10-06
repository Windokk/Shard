#include "world.hpp"

#include <iostream>
#include <algorithm>

#include "engine/world/actor.hpp"
#include "engine/world/components/registry/component_registry.hpp"
#include "engine/world/engine.hpp"
#include "engine/assets/resources_manager.hpp"

namespace Shard::Engine::Worlds{


    World::World(std::string name, Filesystem::Path path)
    {
        this->name = name;
        this->path = path;

        for(auto& [type, factory] : GetWorldExtensionFactories())
            extensions.emplace_back(type, factory());
    }

    void World::NotifyComponentAdded(int idInWorld, const std::shared_ptr<Objects::Components::Component>& component, bool cloned)
    {
        for(auto& [type, extension] : extensions)
            extension->OnComponentAdded(*this, idInWorld, component, cloned);
    }

    void DeserializeComponents(std::shared_ptr<Objects::Actor> a, json actorData){
        if(!actorData["components"].is_array()){
            DEBUG_ERROR("Couldn't deserialize actor's components as actor[components] is not an array");
            return; 
        }

        for(auto& component : actorData["components"]){
            
            if (!component.contains("type")) continue;
        
            const std::string& type = component["type"];

            if(type == "transform"){
                a->GetComponent<Objects::Components::Transform>()->Deserialize(component);
                continue;
            }

            // An engine component (model, light...) or a custom/inherited one, which has to be already registered
            std::shared_ptr<Objects::Components::Component> created = a->AddComponentByName(type);
            if(!created){
                DEBUG_WARNING("Unknown component type: " + type);
                continue;
            }

            created->Deserialize(component);
        }
    }

    void DeserializeActor(std::shared_ptr<Objects::Actor> a, json data, json actor){

        // This actor's own components (crucially its transform) have to be deserialized BEFORE its
        // children's: a child's components (Light, PhysicsBody, ProbeVolume, ...) read the parent's
        // world transform while deserializing themselves (e.g. Light::Deserialize's
        // parent->transform->GetWorldPosition()), and Jolt/GPU-side state built from that read is never
        // refreshed again on its own. Doing children first left every such child using the parent's
        // still-default (identity) transform instead of its actual saved one, so it would sit in the
        // wrong place until something (e.g. moving it manually) recomputed its world transform.
        DeserializeComponents(a, actor);

        if (actor.contains("children") && actor["children"].is_array() && !actor["children"].empty()) {
            Core::IEngineContext* engine = &Core::GetEngine();
            for (auto& child : actor["children"]) {
                std::shared_ptr<Objects::Actor> b = Core::Object::CreateWithContext<Objects::Actor>(engine, child["name"], engine);
                a->AddChild(b);
                DeserializeActor(b, data, child);
            }
        }
    }

    static void CollectActorAssetRefs(const json& actor, std::vector<Core::Resources::ResourceKey>& refs){

        if (actor.contains("children") && actor["children"].is_array()) {
            for (auto& child : actor["children"]) {
                CollectActorAssetRefs(child, refs);
            }
        }

        if (!actor.contains("components") || !actor["components"].is_array())
            return;

        for (auto& component : actor["components"]) {

            if (!component.contains("type") || !component["type"].is_string())
                continue;

            // Only the module that owns a component type knows which of its fields point to assets
            Objects::Components::GetComponentRegistry().CollectAssetRefs(component["type"].get<std::string>(), component, refs);
        }
    }

    std::vector<Core::Resources::ResourceKey> CollectWorldAssetRefs(const Filesystem::Path& filePath)
    {
        std::vector<Core::Resources::ResourceKey> refs;

        std::string src = filePath.ReadFile();

        try {
            json data = json::parse(src);

            if (data.contains("actors") && data["actors"].is_array()) {
                for (auto& actor : data["actors"]) {
                    CollectActorAssetRefs(actor, refs);
                }
            }

            // Same place World::Deserialize reads it from
            if (data.contains("settings") && data["settings"].is_object()) {
                for (auto& collect : GetWorldSettingsAssetRefs())
                    collect(data["settings"], refs);
            }

        } catch (const json::parse_error& e) {
            DEBUG_ERROR("JSON parse error: " + (std::string)e.what());
            refs.clear();
        }

        return refs;
    }

    void World::Deserialize(Filesystem::Path filePath)
    {
        std::string src = filePath.ReadFile();

        try {
            json data = json::parse(src);

            name = data["name"];

            Core::IEngineContext* engine = &Core::GetEngine();
            for(auto& actor : data["actors"])
            {
                std::shared_ptr<Objects::Actor> a = Core::Object::CreateWithContext<Objects::Actor>(engine, actor["name"], engine);
                AddActor(a);
                DeserializeActor(a, data, actor);
            }

            if(data.contains("settings")){
                auto& settings = data["settings"];

                for(auto& [type, extension] : extensions)
                    extension->DeserializeSettings(*this, settings);
            
                if(settings.contains("ambient_intensity")){
                    auto& intensity = settings["ambient_intensity"];
                    if(intensity.is_number()){
                        ambientIntensity = intensity;
                    }
                }

                if(settings.contains("ssao")){
                    auto& ssaoSettings = settings["ssao"];
                    if(ssaoSettings.contains("enabled")){
                        ssaoEnabled = ssaoSettings["enabled"].is_boolean() ? static_cast<bool>(ssaoSettings["enabled"]) : ssaoEnabled;
                    }

                    if(ssaoSettings.contains("radius")){
                        ssaoRadius = ssaoSettings["radius"].is_number_float() ? static_cast<float>(ssaoSettings["radius"]) : ssaoRadius;
                    }

                    if(ssaoSettings.contains("intensity")){
                        ssaoIntensity = ssaoSettings["intensity"].is_number_float() ? static_cast<float>(ssaoSettings["intensity"]) : ssaoIntensity;
                    }

                    if(ssaoSettings.contains("bias")){
                        ssaoBias = ssaoSettings["bias"].is_number_float() ? static_cast<float>(ssaoSettings["bias"]) : ssaoBias;
                    }

                    if(ssaoSettings.contains("power")){
                        ssaoPower = ssaoSettings["power"].is_number_float() ? static_cast<float>(ssaoSettings["power"]) : ssaoPower;
                    }
                }
            }
        
        } catch (const json::parse_error& e) {
            DEBUG_ERROR("JSON parse error: " + (std::string)e.what());
            return;
        }

        dirty = false;
    }

    void SerializeActor(std::shared_ptr<Shard::Engine::Objects::Actor> a, ordered_json* actorsArray){
        
        ordered_json actor;

        actor["name"] = a->GetName();

        for(auto& childrenID : a->GetChildrenID(false)){
            auto child = a->GetChild(childrenID);
            SerializeActor(std::dynamic_pointer_cast<Objects::Actor>(child), &actor["children"]);
        }

        for(auto& comp : a->GetComponents()){
            ordered_json serializedComp = comp->Serialize();
            actor["components"].push_back(serializedComp);
        }

        actorsArray->push_back(actor);
    }

    void World::Serialize(Filesystem::Path filePath)
    {
        ordered_json actorsArray;

        ordered_json meshes;

        ordered_json materials;

        ordered_json full;

        full["name"] = name;

        std::vector<std::pair<Core::ObjectID, std::shared_ptr<Objects::Actor>>> sortedActors(
            rootActors.begin(), rootActors.end()
        );

        std::sort(sortedActors.begin(), sortedActors.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });

        for(auto& [id, actorPtr] : sortedActors){
            SerializeActor(actorPtr, &actorsArray);
        }

        full["actors"] = actorsArray;

        for(auto& [type, extension] : extensions)
            extension->SerializeSettings(*this, full["settings"]);

        full["settings"]["ambient_intensity"] = ambientIntensity;
        full["settings"]["ssao"]["enabled"] = ssaoEnabled;
        full["settings"]["ssao"]["intensity"] = ssaoIntensity;
        full["settings"]["ssao"]["bias"] = ssaoBias;
        full["settings"]["ssao"]["radius"] = ssaoRadius;
        full["settings"]["ssao"]["power"] = ssaoPower;

        std::string fileContent = full.dump();

        filePath.WriteFile(fileContent);

        // Keep the dependency graph (and the asset database's dependency list) in step with the file
        if(auto info = Core::GetEngine().GetAssetIDManager()->GetAssetFromID(assetID))
            Core::GetEngine().GetResourcesManager()->RefreshDependencies(info->baseInfos.nameInProject);

        dirty = false;
    }

    void World::SetBuildIndex(int buildIndex)
    {
        this->buildIndex = buildIndex;
    }

    void World::RemoveActorRecursive(Core::ObjectID actorID)
    {
        auto objPtr = Core::GetEngine().GetObjectIDManager()->GetObjectFromID(actorID);
        auto worldObjPtr = std::dynamic_pointer_cast<Objects::WorldObject>(objPtr);
        if (!worldObjPtr)
            return;

        // Copy children IDs FIRST
        std::vector<Core::ObjectID> children;
        children.reserve(worldObjPtr->GetChildrenCount());

        for (int i = 0; i < worldObjPtr->GetChildrenCount(); ++i)
            children.push_back(worldObjPtr->GetChild(i)->GetID());

        // Now safely recurse
        for (Core::ObjectID childID : children)
            RemoveActorRecursive(childID);

        // Finally remove this actor
        auto actorPtr = std::dynamic_pointer_cast<Objects::Actor>(objPtr);
        if (!actorPtr)
            return;

        actorPtr->Destroy();
    }

    void World::Clear()
    {
        // Make a copy of keys because RemoveActorRecursive modifies rootActors
        std::vector<Core::ObjectID> rootIDs;
        for (auto& [id, actorPtr] : rootActors)
            rootIDs.push_back(id);

        for (Core::ObjectID id : rootIDs)
            RemoveActorRecursive(id);
    }

    void World::OnLoad()
    {
        // What the other modules keep on the world comes back first (the renderer's pass draw lists were
        // cleared on the previous world's unload, so its skybox, cameras and models are re-submitted
        // every load - otherwise a world with a skybox but no actors renders as a black viewport).
        for(auto& [type, extension] : extensions)
            extension->OnLoad(*this);

        for(auto& [id,script] : scripts){
            script->OnWorldLoaded();
        }
    }

    void World::Unload()
    {
        for(auto& [id,script] : scripts){
            script->OnWorldUnloaded();
        }
        
        Clear();

        loaded = false;
    }

    void World::Tick()
    {
        for(auto& [id,script] : scripts){
            if(script->Active()){
                script->OnTick();
            }
        }
    }

    void World::Play()
    {
        for(auto& [type, extension] : extensions)
            extension->OnPlay(*this);

        for(auto& [id,script] : scripts){
            if(script->Active()){
                script->OnPlay();
            }
        }
    }

    void World::Stop()
    {
        for(auto& [id,script] : scripts){
            if(script->Active()){
                script->OnStop();
            }
        }
    }

    void World::AddActor(std::shared_ptr<Objects::Actor> a)
    {
        rootActors.emplace(a->GetID(), a);
        a->SetWorld(this);
    }

    void World::RemoveActor(Core::ObjectID id)
    {
        if(rootActors.find(id) != rootActors.end())
            rootActors.erase(id);
    }

    std::shared_ptr<Objects::Actor> World::GetActor(Core::ObjectID id, bool recursive)
    {
        for (auto& [id, actorPtr] : rootActors)
        {
            if (actorPtr->GetID() == id)
                return actorPtr;

            if(recursive){
                std::vector<Core::ObjectID> children = actorPtr->GetChildrenID(true);

                for(auto& _id : children){
                    std::shared_ptr<Objects::Actor> child = std::dynamic_pointer_cast<Objects::Actor>(Core::GetEngine().GetObjectIDManager()->GetObjectFromID(_id));
                    if(_id == id && child){
                        return child;
                    }
                }
            }
        }

        return nullptr;
    }

    std::vector<Core::ObjectID> World::GetActorsID(bool recursive)
    {
        std::vector<Core::ObjectID> actorIDs;

        for (auto& [id, actorPtr] : rootActors)
        {
            actorIDs.push_back(actorPtr->GetID());

            if(recursive){
                std::vector<Core::ObjectID> children = actorPtr->GetChildrenID(true);
                actorIDs.insert(actorIDs.end(), children.begin(), children.end());
            }
        }

        return actorIDs;
    }

    const std::string &World::GetName() const
    {
        return name;
    }

    void World::SetName(const std::string &name)
    {
        this->name = name;
    }

    void World::RemoveComponent(const int idInWorld, const std::shared_ptr<Objects::Components::Component> compPtr)
    {
        compPtr->Destroy();

        if(compPtr->IsInstanceOf<Objects::Components::Transform>()){
            transforms.erase(idInWorld);
        }
        else if(compPtr->IsInstanceOf<Objects::Components::Script>()){
            scripts.erase(idInWorld);
        }

        for(auto& [type, extension] : extensions)
            extension->OnComponentRemoved(*this, idInWorld, compPtr);
    }
}
