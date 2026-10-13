#include "model_component.hpp"

#include <algorithm>

#include "engine/world/engine.hpp"
#include "engine/assets/resources_manager.hpp"

#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/material/material.hpp"

#include "engine/world/actor.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/renderer/components/render_world_data.hpp"

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

        json data = componentData;

        // Levels saved before the serialization went through the reflection : "mesh" was the mesh's name and
        // "materials" an object of material names by slot ({"0": "...", "2": "..."}), where the fields are
        // "meshID" and "materialsID" (an array, in slot order)
        if (data.is_object())
        {
            if (!data.contains("meshID") && data.contains("mesh"))
                data["meshID"] = data["mesh"];

            if (!data.contains("materialsID") && data.contains("materials"))
            {
                if (!data["materials"].is_object())
                {
                    DEBUG_ERROR("Model has an invalid 'materials' block (not an object)");
                }
                else
                {
                    std::map<int, json> bySlot;
                    for (auto it = data["materials"].begin(); it != data["materials"].end(); ++it)
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

                        bySlot[slot] = it.value();
                    }

                    json ids = json::array();
                    if (!bySlot.empty())
                    {
                        // A slot the file doesn't fill stays empty ("" is no asset)
                        for (int slot = 0; slot <= bySlot.rbegin()->first; ++slot)
                            ids.push_back(bySlot.count(slot) ? bySlot[slot] : json(""));
                    }
                    data["materialsID"] = std::move(ids);
                }
            }
        }

        // The fields only name the assets : loading them (and drawing) is up to the model, in this order
        DeserializeReflectedFields(data, false);

        Filesystem::AssetIDManager* assetIDs = GetEngineContext()->GetAssetIDManager();

        auto nameOf = [&](Filesystem::AssetID id) -> std::string
        {
            std::shared_ptr<Filesystem::AssetInfos> infos = assetIDs->GetAssetFromID(id);
            return infos ? infos->baseInfos.nameInProject : "";
        };

        // ---------------- MESH ----------------
        // Taken before SetMesh/SetMaterials rewrite the ID fields
        const std::vector<Filesystem::AssetID> savedMaterials = materialsID;

        const std::string meshName = nameOf(meshID);
        if (!meshName.empty())
            SetMesh(meshName);

        // ---------------- MATERIALS ----------------
        std::vector<std::shared_ptr<Rendering::Material>> loadedMaterials(savedMaterials.size(), nullptr);

        for (size_t slot = 0; slot < savedMaterials.size(); ++slot)
        {
            const std::string path = nameOf(savedMaterials[slot]);
            if (path.empty())
                continue;

            auto material = GetEngineContext()->GetResourcesManager()->Get<Rendering::Material>(Core::Resources::AssetKind::Material, path);

            if (!material)
            {
                DEBUG_ERROR("Failed to load material: " + path + ", using fallback");

                material = GetEngineContext()->GetResourcesManager()->Get<Rendering::Material>(Core::Resources::AssetKind::Material, "materials/default.mat");

                if (!material)
                {
                    DEBUG_ERROR("Critical: fallback material missing (materials/default.mat)");
                    continue;
                }
            }

            loadedMaterials[slot] = material;
        }

        SetMaterials(std::move(loadedMaterials));

        // ---------------- LIGHTMAP ----------------
        // Where the model's baked lightmap is (written by a bake, see LightmapManager) - not a field, the editor doesn't show it
        lightmapPage = -1;
        lightmapScaleOffset = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
        if (data.contains("lightmapPage") && data["lightmapPage"].is_number_integer() &&
            data.contains("lightmapScaleOffset") && data["lightmapScaleOffset"].is_array() && data["lightmapScaleOffset"].size() == 4)
        {
            lightmapPage = data["lightmapPage"].get<int>();
            for (int i = 0; i < 4; i++)
                lightmapScaleOffset[i] = data["lightmapScaleOffset"][i].get<float>();
        }

        // ---------------- ACTIVE ----------------
        DeserializeActive(data, false);
    }

    ordered_json Model::Serialize()
    {
        // The model's state is its mesh and materials, the ID fields follow them
        if (mesh)
            meshID = mesh->GetAssetID();

        materialsID.clear();
        for (const auto& material : materials)
            materialsID.push_back(material ? material->GetAssetID() : Filesystem::AssetID());

        ordered_json component = SerializeReflected("model");

        if (lightmapPage >= 0)
        {
            component["lightmapPage"] = lightmapPage;
            component["lightmapScaleOffset"] = { lightmapScaleOffset.x, lightmapScaleOffset.y, lightmapScaleOffset.z, lightmapScaleOffset.w };
        }

        return component;
    }

    void Model::SetMesh(std::string meshPath)
    {
        if(!activated)
            return;
        
        std::shared_ptr<Rendering::Mesh> newMesh = GetEngineContext()->GetResourcesManager()->Get<Rendering::Mesh>(Core::Resources::AssetKind::Mesh, meshPath);

        if(newMesh){
            this->mesh = newMesh;
            this->meshID = mesh->GetAssetID();
            lightmapPage = -1;      // the lightmap was laid on the other mesh

            size_t submeshCount = mesh->GetMaterialSlotCount();
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

            this->mesh = GetEngineContext()->GetResourcesManager()->Get<Rendering::Mesh>(Core::Resources::AssetKind::Mesh, name);
            this->meshID = mesh->GetAssetID();
            lightmapPage = -1;      // the lightmap was laid on the other mesh

            size_t submeshCount = mesh->GetMaterialSlotCount();
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
    }

    void Model::SetMaterials(std::vector<std::shared_ptr<Rendering::Material>>&& newMaterials)
    {
        if(!activated)
            return;

        this->materials = std::move(newMaterials);
        materialsID.clear();
        for(int i = 0; i < materials.size(); i++){
            materialsID.push_back(materials[i]->GetAssetID());
        }
    }

    void Model::AddToPass(const std::string &passName)
    {
        if(!activated)
            return;

        if (std::find(extraPasses.begin(), extraPasses.end(), passName) == extraPasses.end())
            extraPasses.push_back(passName);
    }

    void Model::RemoveFromPass(const std::string &passName)
    {
        extraPasses.erase(std::remove(extraPasses.begin(), extraPasses.end(), passName), extraPasses.end());
    }

    void Model::Destroy()
    {
    }

    std::shared_ptr<Component> Model::Clone() const
    {
        auto cloned = Object::Create<Model>(*this);

        // A copy is a different object : it sits somewhere else, which the lightmap of the original does not know
        cloned->lightmapPage = -1;

        return cloned;
    }

    void Model::OnFieldChanged(const FieldChangedEvent &event)
    {
        //Materials
        if(event.field->type == TypeID::Vector && event.field->container){
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

    void ReloadModelsUsingMesh(Worlds::WorldManager& worlds, const std::string& meshNameInProject)
    {
        auto* assetIDs = Core::GetEngine().GetAssetIDManager();

        for (int w = 0; w < worlds.GetLoadedWorldCount(); w++)
        {
            Worlds::World* world = worlds.GetWorldAt(w);
            if (!world)
                continue;

            for (auto& [id, model] : world->Ext<Rendering::RenderWorldData>().models)
            {
                if (!model || !model->MeshRef())
                    continue;

                auto info = assetIDs->GetAssetFromID(model->MeshRef()->GetAssetID());
                if (info && info->baseInfos.nameInProject == meshNameInProject)
                    model->SetMesh(meshNameInProject);
            }
        }
    }
}