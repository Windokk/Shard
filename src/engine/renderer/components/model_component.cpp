#include "model_component.hpp"

#include <algorithm>

#include "engine/world/engine.hpp"
#include "engine/assets/resources_manager.hpp"

#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/material/material.hpp"

#include "engine/world/actor.hpp"
#include "engine/renderer/frontend/render_world_data.hpp"

#include "model_component.reflection.hpp"

#include "engine/renderer/features/lighting/shadow_manager.hpp"

namespace Shard::Engine::Objects::Components{
    

    Model::Model(std::shared_ptr<Actor> parent, uint32_t localID) : Component(parent, localID)
    {
    }

    void Model::Deserialize(const json componentData)
    {
        if (!parent)
            return;

        auto getBool = [&](const char* key, bool fallback = false) -> bool
        {
            if (!componentData.contains(key) || !componentData[key].is_boolean())
                return fallback;
            return componentData[key].get<bool>();
        };

        auto getString = [&](const char* key) -> std::optional<std::string>
        {
            if (!componentData.contains(key) || !componentData[key].is_string())
                return std::nullopt;
            return componentData[key].get<std::string>();
        };

        // ---------------- MESH ----------------
        if (auto mesh = getString("mesh"))
        {
            SetMesh(*mesh);
        }

        // ---------------- MATERIALS ----------------
        std::vector<std::shared_ptr<Rendering::Material>> materials;

        if (componentData.contains("materials") && !componentData["materials"].is_object())
        {
            // A missing key is a legitimate "no materials yet" model (see the comment in Serialize()) -
            // only a present-but-wrong-typed block indicates actual corruption worth flagging.
            DEBUG_ERROR("Model has an invalid 'materials' block (not an object)");
        }
        else if (componentData.contains("materials"))
        {
            const auto& localMaterials = componentData["materials"];

            int maxSlot = -1;
            std::vector<std::pair<int, std::string>> parsed;

            // First pass: validate + collect safely
            for (auto it = localMaterials.begin(); it != localMaterials.end(); ++it)
            {
                int slot = 0;

                try
                {
                    slot = std::stoi(it.key());
                }
                catch (...)
                {
                    DEBUG_ERROR("Invalid material slot key (not integer): " + it.key());
                    continue;
                }

                if (slot < 0)
                {
                    DEBUG_ERROR("Negative material slot ignored: " + it.key());
                    continue;
                }

                if (!it.value().is_string())
                {
                    DEBUG_ERROR("Material path is not a string at slot: " + it.key());
                    continue;
                }

                std::string path = it.value().get<std::string>();

                parsed.emplace_back(slot, std::move(path));
                if (slot > maxSlot)
                    maxSlot = slot;
            }

            if (maxSlot >= 0)
                materials.resize(maxSlot + 1, nullptr);

            // Second pass: load materials
            for (auto& [slot, path] : parsed)
            {
                auto material =
                    GetEngineContext()->GetResourcesManager()->Get<Rendering::Material>(Core::Resources::AssetKind::Material, path);

                if (!material)
                {
                    DEBUG_ERROR("Failed to load material: " + path + ", using fallback");

                    material =
                        GetEngineContext()->GetResourcesManager()->Get<Rendering::Material>(Core::Resources::AssetKind::Material, "materials/default.mat");

                    if (!material)
                    {
                        DEBUG_ERROR("Critical: fallback material missing (materials/default.mat)");
                        continue;
                    }
                }

                materials[slot] = material;
            }
        }

        SetMaterials(std::move(materials));

        // ---------------- ACTIVE ----------------
        if (getBool("active", false))
            Activate();
        else
            DeActivate();
    }

    ordered_json Model::Serialize()
    {
        ordered_json comp;

        comp["type"] = "model";

        comp["active"] = activated;

        comp["mesh"] = GetEngineContext()->GetAssetIDManager()->GetAssetFromID(mesh->GetAssetID())->baseInfos.nameInProject;

        // Always emit the key, even with zero materials - nlohmann::json only creates "materials" the
        // first time comp["materials"][...] is assigned, so a model with no materials yet (e.g. right
        // after AddComponent<Model>(), before a mesh/material is picked) would otherwise round-trip
        // through save/load with the key missing entirely, which Deserialize below logs as corruption.
        comp["materials"] = ordered_json::object();
        for(int i = 0; i < materials.size(); i++){
            std::string matNameInProject = GetEngineContext()->GetAssetIDManager()->GetAssetFromID(materials[i]->GetAssetID())->baseInfos.nameInProject;
            comp["materials"][std::to_string(i)] = matNameInProject;
        }

        return comp;
    }

    void Model::SetMesh(std::string meshPath)
    {
        if(!activated)
            return;
        
        std::shared_ptr<Rendering::Mesh> newMesh = GetEngineContext()->GetResourcesManager()->Get<Rendering::Mesh>(Core::Resources::AssetKind::Mesh, meshPath);

        if(newMesh){
            if(mesh)
                RemoveFromDrawList();
            this->mesh = newMesh;
            this->meshID = mesh->GetAssetID();

            size_t submeshCount = mesh->GetSubMeshes().size();
            if(materials.size() != submeshCount)
            {
                std::vector<std::shared_ptr<Rendering::Material>> resized(submeshCount, nullptr);
                for(size_t i = 0; i < submeshCount && i < materials.size(); i++)
                    resized[i] = materials[i];

                std::shared_ptr<Rendering::Material> defaultMaterial =
                    GetEngineContext()->GetResourcesManager()->Get<Rendering::Material>(Core::Resources::AssetKind::Material, "materials/default.mat");

                for(auto& mat : resized)
                {
                    if(!mat)
                        mat = defaultMaterial;
                }

                SetMaterials(std::move(resized));
            }

            UpdateReferenceInWorld();

            // Re-register the new mesh into whatever extra passes it was in before the swap (e.g. this
            // is the currently selected actor's EditorOutlineMaskPass entry) - RemoveFromDrawList()
            // above already dropped the old mesh's commands from them.
            for(const auto& passName : std::vector<std::string>(extraPasses))
                AddToPass(passName);
        }
        else{
            DEBUG_ERROR("Specified mesh path doesn't exist in project");
        }
    }

    void Model::SetMesh(Filesystem::AssetID meshID)
    {
        if(!activated)
            return;

        std::shared_ptr<Filesystem::AssetInfos> asset = GetEngineContext()->GetAssetIDManager()->GetAssetFromID(meshID);
        
        if(asset){
            std::string name = asset->baseInfos.nameInProject;

            if(mesh)
                RemoveFromDrawList();
            this->mesh = GetEngineContext()->GetResourcesManager()->Get<Rendering::Mesh>(Core::Resources::AssetKind::Mesh, name);
            this->meshID = mesh->GetAssetID();

            size_t submeshCount = mesh->GetSubMeshes().size();
            if(materials.size() != submeshCount)
            {
                std::vector<std::shared_ptr<Rendering::Material>> resized(submeshCount, nullptr);
                for(size_t i = 0; i < submeshCount && i < materials.size(); i++)
                    resized[i] = materials[i];

                std::shared_ptr<Rendering::Material> defaultMaterial =
                    GetEngineContext()->GetResourcesManager()->Get<Rendering::Material>(Core::Resources::AssetKind::Material, "materials/default.mat");

                for(auto& mat : resized)
                {
                    if(!mat)
                        mat = defaultMaterial;
                }

                SetMaterials(std::move(resized));
            }

            this->Update();
            UpdateReferenceInWorld();

            // Re-register the new mesh into whatever extra passes it was in before the swap (e.g. this
            // is the currently selected actor's EditorOutlineMaskPass entry) - RemoveFromDrawList()
            // above already dropped the old mesh's commands from them.
            for(const auto& passName : std::vector<std::string>(extraPasses))
                AddToPass(passName);
        }
    }

    void Model::OnTransformChanged(uint8_t)
    {
        UpdateReferenceInWorld();
    }

    void Model::UpdateReferenceInWorld()
    {
        if (!parent || !parent->world || !mesh || !activated)
        {
            return;
        }
        parent->world->Ext<Rendering::RenderWorldData>().meshes[parent->GetComponentIDInWorld(local_id)] = { parent->transform->GetWorldMatrix(), mesh.get() };
        
        Update();
    }

    void Model::SetMaterials(std::vector<std::shared_ptr<Rendering::Material>>&& newMaterials)
    {
        if(!activated)
            return;

        this->materials = std::move(newMaterials);
        this->Update();
        materialsID.clear();
        for(int i = 0; i < materials.size(); i++){
            materialsID.push_back(materials[i]->GetAssetID());
        }
    }

    void Model::Update(){

        if(!activated)
            return;

        if (materials.size() > 0 && mesh != nullptr && parent->world && parent->world->IsLoaded()){

            std::shared_ptr<Transform> tr = parent->transform;

            std::vector<Rendering::DrawCommand> cmds = mesh->CreateDrawCommands(tr, parent->GetComponentIDInWorld(local_id), this->materials);

            std::vector<std::string> passesName;
            passesName.push_back("ForwardPass");
            // SSAO's depth+normal prepass (see SSAOManager) needs every mesh's real geometry, the same
            // way ForwardPass does - it always runs (SSAOManager::Init registers it unconditionally),
            // only World::ssaoEnabled gates whether lit.frag actually uses the result.
            passesName.push_back("SSAODepthNormalPass");

            GetEngineContext()->GetRenderer()->AddOrUpdateCommands(cmds, passesName, true);
        }
    }

    void Model::AddToPass(const std::string &passName)
    {
        if(!activated)
            return;

        if (std::find(extraPasses.begin(), extraPasses.end(), passName) == extraPasses.end())
            extraPasses.push_back(passName);

        if (materials.size() > 0 && mesh != nullptr && parent->world && parent->world->IsLoaded()){

            std::shared_ptr<Transform> tr = parent->transform;

            std::vector<Rendering::DrawCommand> cmds = mesh->CreateDrawCommands(tr, parent->GetComponentIDInWorld(local_id), this->materials);

            GetEngineContext()->GetRenderer()->AddOrUpdateCommands(cmds, {passName}, false);
        }
    }

    void Model::RemoveFromPass(const std::string &passName)
    {
        extraPasses.erase(std::remove(extraPasses.begin(), extraPasses.end(), passName), extraPasses.end());

        if (materials.size() > 0 && mesh != nullptr && parent->world && parent->world->IsLoaded()){

            std::vector<uint64_t> cmdsID;
            for(int i = 0; i < mesh->GetSubMeshes().size(); i++){
                cmdsID.push_back(Rendering::MakeCommandID(mesh->GetAssetID().GetAsInt(), parent->GetComponentIDInWorld(local_id), i));
            }

            GetEngineContext()->GetRenderer()->RemoveCommands(cmdsID, {passName}, false);
        }
    }

    void Model::Activate()
    {
        bool wasInactive = !activated;
        Component::Activate();

        // Re-issue the draw commands that DeActivate() pulled - Update() itself is a no-op
        // if there's no mesh/materials yet, so this is safe to call unconditionally here.
        if(wasInactive)
            Update();
    }

    void Model::DeActivate()
    {
        if(!activated){
            Component::DeActivate();
            return;
        }

        Component::DeActivate();
        RemoveFromDrawList();
    }

    void Model::RemoveFromDrawList()
    {
        if (materials.size() > 0 && mesh != nullptr && parent->world->IsLoaded()){

            std::shared_ptr<Transform> tr = parent->transform;
            std::vector<uint64_t> cmdsID;
            for(int i = 0; i < mesh->GetSubMeshes().size(); i++){
                cmdsID.push_back(Rendering::MakeCommandID(mesh->GetAssetID().GetAsInt(), parent->GetComponentIDInWorld(local_id), i));
            }

            std::vector<std::string> passesName;
            passesName.push_back("ForwardPass");
            passesName.push_back("SSAODepthNormalPass");
            GetEngineContext()->GetRenderer()->RemoveCommands(cmdsID, passesName, true);

            // Also drop this (still-current, about-to-be-replaced-or-gone) mesh's commands from any
            // extra pass it was registered into (e.g. EditorOutlineMaskPass) - otherwise a mesh swap
            // on a selected actor leaves the old mesh's commands orphaned in that pass forever, since
            // a later RemoveFromPass() would compute IDs from the *new* mesh instead.
            if(!extraPasses.empty())
                GetEngineContext()->GetRenderer()->RemoveCommands(cmdsID, extraPasses, false);
        }
    }

    void Model::Destroy()
    {
        RemoveFromDrawList();
    }

    std::shared_ptr<Component> Model::Clone() const
    {
        auto cloned = Object::Create<Model>(*this);

        return cloned;
    }

    void Model::OnFieldChanged(const FieldChangedEvent &event)
    {
        //Materials
        if(event.field->type == TypeID::Vector && event.field->container){
            Update();
            std::vector<std::shared_ptr<Rendering::Material>> mats = {};
            for(auto matID : materialsID){
                mats.push_back(GetEngineContext()->GetResourcesManager()->Get<Rendering::Material>(Core::Resources::AssetKind::Material, GetEngineContext()->GetAssetIDManager()->GetAssetFromID(matID)->baseInfos.nameInProject));
            }
            SetMaterials(std::move(mats));
        }
        
        //Mesh
        else if(event.field->type == TypeID::Asset){
            SetMesh(meshID);
        }
    }
}