#include "world.hpp"

#include <iostream>
#include <algorithm>

#include "engine/world/actor.hpp"
#include "engine/world/components/registry/component_registry.hpp"
#include "engine/world/engine.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/assets/serialization/tags/tag_json.hpp"
#include "engine/core/tags/tag_registry.hpp"
#include "engine/world/event_system.hpp"

namespace Shard::Engine::Worlds{

    namespace {
        /// Calls `fn(script)` for the scripts the world has now, in the order of their ids. A script may destroy actors (itself included) or add
        /// components while it runs: the ones removed meanwhile are skipped, the ones added start with the next call.
        template <typename Fn>
        void ForEachScript(std::map<int, std::shared_ptr<Objects::Components::Script>>& scripts, Fn&& fn)
        {
            std::vector<std::pair<int, std::shared_ptr<Objects::Components::Script>>> snapshot(scripts.begin(), scripts.end());
            for(auto& [id, script] : snapshot){
                auto it = scripts.find(id);
                if(it == scripts.end() || it->second != script)
                    continue;
                fn(script);
            }
        }
    }


    World::World(std::string name, Filesystem::Path path)
    {
        this->name = name;
        this->path = path;

        for(auto& [type, factory] : GetWorldExtensionFactories())
            extensions.emplace_back(type, factory());

        tags.SetListener(this);
    }

    void World::NotifyComponentAdded(int idInWorld, const std::shared_ptr<Objects::Components::Component>& component, bool cloned)
    {
        for(auto& [type, extension] : extensions)
            extension->OnComponentAdded(*this, idInWorld, component, cloned);
    }

    static void DeserializeComponent(const std::shared_ptr<Objects::Actor>& a, const std::string& type, const json& component){
        if(type == "transform"){
            auto transform = a->GetComponent<Objects::Components::Transform>();
            transform->Deserialize(component);
            if(component.contains("tags"))
                transform->GetTags().Assign(Serialization::TagsFromJson(component["tags"]), true);
            return;
        }

        // An engine component (model, light...) or a custom/inherited one, which has to be already registered
        std::shared_ptr<Objects::Components::Component> created = a->AddComponentByName(type);
        if(!created){
            // Kept, not dropped : the plugin that brings this type may just not be loaded, and a save must not lose its data
            DEBUG_WARNING("Unknown component type: " + type + " (kept as it is in the file)");
            a->AddUnknownComponent(component);
            return;
        }

        created->Deserialize(component);

        if(component.contains("tags"))
            created->GetTags().Assign(Serialization::TagsFromJson(component["tags"]), true);
    }

    void DeserializeComponents(std::shared_ptr<Objects::Actor> a, json actorData){
        if(!actorData.contains("components") || !actorData["components"].is_array()){
            DEBUG_ERROR("Couldn't deserialize actor's components as actor[components] is not an array");
            return;
        }

        for(auto& component : actorData["components"]){

            if (!component.is_object() || !component.contains("type")) continue;

            if (!component["type"].is_string()) {
                DEBUG_WARNING("Skipped a component of actor " + a->GetName() + " : its type is not a string");
                continue;
            }
            const std::string type = component["type"].get<std::string>();

            // A component whose data has the wrong shape is dropped alone : the rest of the actor, and of the world, still loads
            try {
                DeserializeComponent(a, type, component);
            } catch (const json::exception& e) {
                DEBUG_WARNING("Couldn't deserialize component " + type + " of actor " + a->GetName() + " : " + e.what());
            }
        }
    }

    // The name an actor is created with : a file whose name is missing or is not a string still loads, with a default one
    static std::string ActorNameFromJson(const json& actor){
        if (actor.contains("name") && actor["name"].is_string())
            return actor["name"].get<std::string>();
        DEBUG_WARNING("An actor has no name (or a name that is not a string) : named \"Actor\"");
        return "Actor";
    }

    void DeserializeActor(std::shared_ptr<Objects::Actor> a, json data, json actor){

        // Before the components : the scripts' OnCreate (run as each one joins the world) can read the actor's tags
        if (actor.contains("tags"))
            a->GetTags().Assign(Serialization::TagsFromJson(actor["tags"]), true);

        // This actor's own components (crucially its transform) have to be deserialized BEFORE its
        // children's: a child's components (Light, PhysicsBody, ProbeVolume, ...) read the parent's
        // world transform while deserializing themselves (e.g. Light::Deserialize's
        // parent->transform->GetWorldPosition()), and Jolt/GPU-side state built from that read is never
        // refreshed again on its own. Doing children first left every such child using the parent's
        // still-default (identity) transform instead of its actual saved one, so it would sit in the
        // wrong place until something (e.g. moving it manually) recomputed its world transform.
        DeserializeComponents(a, actor);

        if (actor.contains("folder") && actor["folder"].is_string())
            a->SetFolder(actor["folder"].get<std::string>());

        if (actor.contains("children") && actor["children"].is_array() && !actor["children"].empty()) {
            Core::IEngineContext* engine = &Core::GetEngine();
            for (auto& child : actor["children"]) {
                if (!child.is_object()) {
                    DEBUG_WARNING("Skipped a child of actor " + a->GetName() + " : it is not an object");
                    continue;
                }
                std::shared_ptr<Objects::Actor> b = Core::Object::CreateWithContext<Objects::Actor>(engine, ActorNameFromJson(child), engine);
                a->AddChild(b);
                DeserializeActor(b, data, child);
            }
        }
    }

    static void CollectActorAssetRefs(const json& actor, std::vector<Core::Resources::ResourceKey>& refs){

        if (!actor.is_object())
            return;

        if (actor.contains("children") && actor["children"].is_array()) {
            for (auto& child : actor["children"]) {
                CollectActorAssetRefs(child, refs);
            }
        }

        if (!actor.contains("components") || !actor["components"].is_array())
            return;

        for (auto& component : actor["components"]) {

            if (!component.is_object() || !component.contains("type") || !component["type"].is_string())
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
            ApplyWorldJsonFilters(filePath.GetFilename(), data);

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
        } catch (const json::exception& e) {
            // What it collected so far is kept : those assets are wanted whatever comes after
            DEBUG_ERROR("Couldn't read the assets of world " + filePath.GetFilename() + " : " + e.what());
        }

        return refs;
    }

    namespace {
        ordered_json SerializeFrames(const Core::ReferenceFrameTree& tree)
        {
            ordered_json array = ordered_json::array();
            for (const Core::ReferenceFrame& f : tree.GetFrames()) {
                if (f.id == Core::kRootFrame)
                    continue;
                ordered_json frame;
                frame["id"] = f.id;
                frame["name"] = f.name;
                frame["parent"] = f.parent;
                frame["cell"] = { f.origin.cell.x, f.origin.cell.y, f.origin.cell.z };
                frame["local"] = { f.origin.local.x, f.origin.local.y, f.origin.local.z };
                frame["rotation"] = { f.rotation.w, f.rotation.x, f.rotation.y, f.rotation.z };
                array.push_back(frame);
            }
            return array;
        }

        void DeserializeFrames(Core::ReferenceFrameTree& tree, const json& array)
        {
            tree.Clear();
            if (!array.is_array())
                return;

            // A frame can name a parent that comes later in the file : insert until nothing more can be
            std::vector<const json*> pending;
            for (const json& frame : array)
                pending.push_back(&frame);

            bool progress = true;
            while (!pending.empty() && progress) {
                progress = false;
                for (auto it = pending.begin(); it != pending.end();) {
                    const json& f = **it;
                    const Core::FrameId parent = f.value("parent", Core::kRootFrame);
                    if (!tree.Exists(parent)) {
                        ++it;
                        continue;
                    }

                    Core::CellCoord cell;
                    glm::dvec3 local(0.0);
                    glm::dquat rotation(1.0, 0.0, 0.0, 0.0);
                    if (f.contains("cell") && f["cell"].is_array() && f["cell"].size() == 3)
                        cell = { f["cell"][0].get<int64_t>(), f["cell"][1].get<int64_t>(), f["cell"][2].get<int64_t>() };
                    if (f.contains("local") && f["local"].is_array() && f["local"].size() == 3)
                        local = glm::dvec3(f["local"][0].get<double>(), f["local"][1].get<double>(), f["local"][2].get<double>());
                    if (f.contains("rotation") && f["rotation"].is_array() && f["rotation"].size() == 4)
                        rotation = glm::dquat(f["rotation"][0].get<double>(), f["rotation"][1].get<double>(), f["rotation"][2].get<double>(), f["rotation"][3].get<double>());

                    tree.CreateWithId(f.value("id", Core::kInvalidFrame), f.value("name", std::string()), parent,
                                      Core::WorldPosition(cell, local), rotation);
                    it = pending.erase(it);
                    progress = true;
                }
            }
        }
    }

    void World::Deserialize(Filesystem::Path filePath)
    {
        std::string src = filePath.ReadFile();

        try {
            json data = json::parse(src);
            ApplyWorldJsonFilters(filePath.GetFilename(), data);

            if(!data.is_object()){
                DEBUG_ERROR("Couldn't load world " + filePath.GetFilename() + " : the file is not a JSON object");
                return;
            }

            if(data.contains("name") && data["name"].is_string())
                name = data["name"].get<std::string>();
            else{
                name = filePath.GetFilename(false);
                DEBUG_WARNING("World " + filePath.GetFilename() + " has no name (or a name that is not a string) : named " + name);
            }

            tags.Assign(data.contains("tags") ? Serialization::TagsFromJson(data["tags"]) : Core::TagContainer(), true);

            folders.clear();
            if(data.contains("folders") && data["folders"].is_array())
                for(auto& folder : data["folders"])
                    if(folder.is_string())
                        folders.push_back(folder.get<std::string>());

            // Before the actors : a transform names its frame by id
            if(data.contains("frames"))
                DeserializeFrames(frames, data["frames"]);
            else
                frames.Clear();

            Core::IEngineContext* engine = &Core::GetEngine();
            const json noActors = json::array();
            const json& actors = data.contains("actors") && data["actors"].is_array() ? data["actors"] : noActors;
            for(auto& actor : actors)
            {
                if(!actor.is_object()){
                    DEBUG_WARNING("Skipped an entry of the actors of world " + name + " : it is not an object");
                    continue;
                }
                std::shared_ptr<Objects::Actor> a = Core::Object::CreateWithContext<Objects::Actor>(engine, ActorNameFromJson(actor), engine);
                AddActor(a);
                DeserializeActor(a, data, actor);
            }

            if(data.contains("settings")){
                auto& settings = data["settings"];

                for(auto& [type, extension] : extensions)
                    extension->DeserializeSettings(*this, settings);
            
                // "ambient_intensity" (a single number) is what worlds saved before the ambient had a color hold
                if(settings.contains("ambient_intensity") && settings["ambient_intensity"].is_number()){
                    ambientColor = glm::vec3(settings["ambient_intensity"].get<float>());
                }
                if(settings.contains("ambient_color")){
                    auto& color = settings["ambient_color"];
                    if(color.is_array() && color.size() == 3 && color[0].is_number() && color[1].is_number() && color[2].is_number()){
                        ambientColor = glm::vec3(color[0].get<float>(), color[1].get<float>(), color[2].get<float>());
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
        } catch (const json::exception& e) {
            // Thrown after the parse : the world keeps what was read before (the actors are dropped one component at a time
            // already, so this is the world's own data : settings, tags, frames). Not dirty : nothing was edited yet, and a dirty
            // world would block the next hot reload, the one that brings the fixed file
            DEBUG_ERROR("World " + filePath.GetFilename() + " only partially loaded : " + e.what());
        }

        dirty = false;
    }

    void SerializeActor(std::shared_ptr<Shard::Engine::Objects::Actor> a, ordered_json* actorsArray){
        
        ordered_json actor;

        actor["name"] = a->GetName();

        if(!a->GetFolder().empty())
            actor["folder"] = a->GetFolder();

        if(!a->GetTags().IsEmpty())
            actor["tags"] = Serialization::TagsToJson(a->GetTags());

        for(auto& childrenID : a->GetChildrenID(false)){
            auto child = a->GetChild(childrenID);
            SerializeActor(std::dynamic_pointer_cast<Objects::Actor>(child), &actor["children"]);
        }

        for(auto& comp : a->GetComponents()){
            ordered_json serializedComp = comp->Serialize();
            // For every component, whatever its Serialize does : "tags" is a reserved key (see Component::SerializeReflected)
            if(!comp->GetTags().IsEmpty())
                serializedComp["tags"] = Serialization::TagsToJson(comp->GetTags());
            actor["components"].push_back(serializedComp);
        }
        for(const json& unknown : a->GetUnknownComponents())
            actor["components"].push_back(ordered_json(unknown));

        actorsArray->push_back(actor);
    }

    ordered_json SerializeActorToJson(const std::shared_ptr<Objects::Actor>& actor)
    {
        ordered_json array = ordered_json::array();
        if (actor)
            SerializeActor(actor, &array);
        return array.empty() ? ordered_json::object() : array[0];
    }

    std::shared_ptr<Objects::Actor> InstantiateActor(World& world, const json& actorData, const std::shared_ptr<Objects::Actor>& parent)
    {
        if (!actorData.is_object() || !actorData.contains("name") || !actorData["name"].is_string())
            return nullptr;

        Core::IEngineContext* engine = &Core::GetEngine();
        std::shared_ptr<Objects::Actor> a = Core::Object::CreateWithContext<Objects::Actor>(engine, actorData["name"].get<std::string>(), engine);

        // Placed first, like World::Deserialize does : the components read their parents' world transform while they deserialize
        if (parent)
            parent->AddChild(a);
        else
            world.AddActor(a);

        DeserializeActor(a, json::object(), actorData);
        return a;
    }

    void World::Serialize(Filesystem::Path filePath)
    {
        if(!AllowWorldSave(filePath.GetFilename())){
            DEBUG_WARNING("Saving ", filePath.GetFilename(), " was cancelled");
            return;
        }

        ordered_json actorsArray;

        ordered_json meshes;

        ordered_json materials;

        ordered_json full;

        full["name"] = name;

        if(!tags.IsEmpty())
            full["tags"] = Serialization::TagsToJson(tags);

        if(frames.Count() > 1)
            full["frames"] = SerializeFrames(frames);

        if(!folders.empty())
            full["folders"] = folders;

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

        full["settings"]["ambient_color"] = { ambientColor.x, ambientColor.y, ambientColor.z };
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

    int World::ResolveAssetID()
    {
        if(resolvedAssetID >= 0 && resolvedAssetPath == path.full)
            return resolvedAssetID;

        auto& engine = Core::GetEngine();
        const auto& assets = engine.GetAssetIDManager()->AssetIDMap;
        if(resolvedAssetMissing && resolvedAssetPath == path.full && resolvedAssetCount == assets.size())
            return -1;

        const std::string nameInProject = engine.GetFileManager()->GetFileInfos(path).nameInProject;
        if(!nameInProject.empty())
            for(const auto& [id, info] : assets)
                if(info && info->baseInfos.nameInProject == nameInProject){
                    resolvedAssetID = id.GetAsInt();
                    resolvedAssetPath = path.full;
                    resolvedAssetMissing = false;
                    return resolvedAssetID;
                }

        resolvedAssetID = -1;
        resolvedAssetPath = path.full;
        resolvedAssetMissing = true;
        resolvedAssetCount = assets.size();
        return -1;
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

        frames.Clear();
        folders.clear();

        tags.Assign(Core::TagContainer(), true);
        actorsByTag.clear();
        componentsByTag.clear();
    }

    void World::OnLoad()
    {
        // What the other modules keep on the world comes back first (the renderer's pass draw lists were
        // cleared on the previous world's unload, so its skybox, cameras and models are re-submitted
        // every load - otherwise a world with a skybox but no actors renders as a black viewport).
        for(auto& [type, extension] : extensions)
            extension->OnLoad(*this);

        ForEachScript(scripts, [](auto& script){ script->OnWorldLoaded(); });
    }

    void World::Unload()
    {
        for(auto& [type, extension] : extensions)
            extension->OnUnload(*this);

        ForEachScript(scripts, [](auto& script){ script->OnWorldUnloaded(); });
        
        Clear();

        loaded = false;
    }

    void World::Tick()
    {
        ForEachScript(scripts, [](auto& script){ if(script->Active()) script->OnTick(); });
    }

    void World::FixedTick()
    {
        ForEachScript(scripts, [](auto& script){ if(script->Active()) script->OnFixedTick(); });
    }

    void World::LateTick()
    {
        ForEachScript(scripts, [](auto& script){ if(script->Active()) script->OnLateTick(); });
    }

    void World::Play()
    {
        for(auto& [type, extension] : extensions)
            extension->OnPlay(*this);

        ForEachScript(scripts, [](auto& script){ if(script->Active()) script->OnPlay(); });
    }

    void World::Stop()
    {
        ForEachScript(scripts, [](auto& script){ if(script->Active()) script->OnStop(); });

        for(auto& [type, extension] : extensions)
            extension->OnStop(*this);
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

        IndexComponentTags(idInWorld, compPtr, Core::TagContainer(), compPtr->GetTags());

        for(auto& [type, extension] : extensions)
            extension->OnComponentRemoved(*this, idInWorld, compPtr);
    }

    // ---- Tags ----

    void World::OnTagsChanged(const Core::TagContainer& added, const Core::TagContainer& removed, bool quiet)
    {
        if (quiet || !loaded)
            return;
        if (Events::EventDispatcher* events = Core::GetEngine().GetEventDispatcher())
            events->emitToWorld(ResolveAssetID(), Events::TagsChangedEvent(added, removed, Core::ObjectID(0)));
    }

    void World::IndexActorTags(const std::shared_ptr<Objects::Actor>& actor, const Core::TagContainer& added, const Core::TagContainer& removed)
    {
        if (!actor)
            return;

        const Core::ObjectID actorID = actor->GetID();
        for (Core::Tag tag : removed) {
            auto it = actorsByTag.find(tag.GetId().Value());
            if (it == actorsByTag.end())
                continue;
            it->second.erase(actorID);
            if (it->second.empty())
                actorsByTag.erase(it);
        }
        for (Core::Tag tag : added)
            actorsByTag[tag.GetId().Value()][actorID] = actor;
    }

    void World::IndexComponentTags(int idInWorld, const std::shared_ptr<Objects::Components::Component>& component,
                                   const Core::TagContainer& added, const Core::TagContainer& removed)
    {
        for (Core::Tag tag : removed) {
            auto it = componentsByTag.find(tag.GetId().Value());
            if (it == componentsByTag.end())
                continue;
            it->second.erase(idInWorld);
            if (it->second.empty())
                componentsByTag.erase(it);
        }
        if (!component)
            return;
        for (Core::Tag tag : added)
            componentsByTag[tag.GetId().Value()][idInWorld] = component;
    }

    std::map<Core::ObjectID, std::shared_ptr<Objects::Actor>> World::CollectTagged(Core::Tag tag, bool includeInactive)
    {
        std::map<Core::ObjectID, std::shared_ptr<Objects::Actor>> found;
        if (!tag.IsValid())
            return found;

        // The actors tagged `tag` itself, or a tag under it
        std::vector<Core::Tag> tree;
        Core::TagRegistry::Get().CollectDescendants(tag, tree);

        for (Core::Tag t : tree) {
            auto it = actorsByTag.find(t.GetId().Value());
            if (it == actorsByTag.end())
                continue;
            for (auto& [id, weak] : it->second) {
                std::shared_ptr<Objects::Actor> actor = weak.lock();
                if (actor && (includeInactive || actor->IsActive()))
                    found.emplace(id, std::move(actor));
            }
        }
        return found;
    }

    std::map<Core::ObjectID, std::shared_ptr<Objects::Actor>> World::CollectAll(bool includeInactive)
    {
        std::map<Core::ObjectID, std::shared_ptr<Objects::Actor>> found;

        std::vector<std::shared_ptr<Objects::Actor>> pending;
        for (auto& [id, actor] : rootActors)
            pending.push_back(actor);

        while (!pending.empty()) {
            std::shared_ptr<Objects::Actor> actor = std::move(pending.back());
            pending.pop_back();
            if (!actor)
                continue;
            for (Core::ObjectID childID : actor->GetChildrenID(false))
                if (auto child = std::dynamic_pointer_cast<Objects::Actor>(actor->GetChild(childID)))
                    pending.push_back(child);
            if (includeInactive || actor->IsActive())
                found.emplace(actor->GetID(), std::move(actor));
        }
        return found;
    }

    std::vector<std::shared_ptr<Objects::Actor>> World::FindActorsWithTag(Core::Tag tag, bool includeInactive)
    {
        std::vector<std::shared_ptr<Objects::Actor>> out;
        for (auto& [id, actor] : CollectTagged(tag, includeInactive))
            out.push_back(std::move(actor));
        return out;
    }

    std::shared_ptr<Objects::Actor> World::FindFirstActorWithTag(Core::Tag tag, bool includeInactive)
    {
        auto found = CollectTagged(tag, includeInactive);
        return found.empty() ? nullptr : found.begin()->second;
    }

    std::vector<std::shared_ptr<Objects::Actor>> World::FindActors(const Core::TagQuery& query, bool includeInactive)
    {
        const Core::Tag required = query.GetRequiredTag();
        auto candidates = required.IsValid() ? CollectTagged(required, includeInactive) : CollectAll(includeInactive);

        std::vector<std::shared_ptr<Objects::Actor>> out;
        for (auto& [id, actor] : candidates)
            if (query.Matches(actor->GetTags()))
                out.push_back(std::move(actor));
        return out;
    }

    std::vector<std::shared_ptr<Objects::Components::Component>> World::FindComponentsWithTag(Core::Tag tag, bool includeInactive)
    {
        std::vector<std::shared_ptr<Objects::Components::Component>> out;
        if (!tag.IsValid())
            return out;

        std::vector<Core::Tag> tree;
        Core::TagRegistry::Get().CollectDescendants(tag, tree);

        std::map<int, std::shared_ptr<Objects::Components::Component>> found;
        for (Core::Tag t : tree) {
            auto it = componentsByTag.find(t.GetId().Value());
            if (it == componentsByTag.end())
                continue;
            for (auto& [id, weak] : it->second) {
                std::shared_ptr<Objects::Components::Component> component = weak.lock();
                if (component && (includeInactive || component->Active()))
                    found.emplace(id, std::move(component));
            }
        }

        out.reserve(found.size());
        for (auto& [id, component] : found)
            out.push_back(std::move(component));
        return out;
    }

    void World::QuerySphere(const Core::DVec3& center, double radius, const Core::TagQuery& query,
                            std::vector<std::shared_ptr<Objects::Actor>>& out, bool includeInactive)
    {
        out.clear();
        const double radiusSquared = radius * radius;
        for (auto& actor : FindActors(query, includeInactive)) {
            if (!actor->transform)
                continue;
            const Core::DVec3 offset = actor->transform->GetWorldCoords().ToDVec3() - center;
            if (glm::dot(offset, offset) <= radiusSquared)
                out.push_back(std::move(actor));
        }
    }

    void World::QueryAABB(const Core::AABB& box, const Core::TagQuery& query,
                          std::vector<std::shared_ptr<Objects::Actor>>& out, bool includeInactive)
    {
        out.clear();
        for (auto& actor : FindActors(query, includeInactive))
            if (actor->transform && box.Contains(actor->transform->GetWorldCoords().ToDVec3()))
                out.push_back(std::move(actor));
    }
}
