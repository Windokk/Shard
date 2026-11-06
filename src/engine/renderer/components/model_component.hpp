#pragma once

#include "engine/world/components/component.hpp"

#include "engine/assets/reflection/attributes.hpp"

#include "engine/renderer/rhi/resources/mesh/mesh.hpp"

namespace Shard::Engine::Rendering{
    class Mesh;
    class Material;
}

namespace Shard::Engine::Worlds { class WorldManager; }

namespace Shard::Engine::Objects::Components
{
    class CLASS() Model : public Component{
        public:
            Model(std::shared_ptr<Actor> parent, uint32_t localID);

            public:

            void Deserialize(const json componentData) override;

            ordered_json Serialize() override;
            
            void SetMesh(std::string meshPath);
            void SetMesh(Filesystem::AssetID mesh);
            void UpdateReferenceInWorld();

            void OnTransformChanged(uint8_t changes) override;
            void SetMaterials(std::vector<std::shared_ptr<Rendering::Material>> &&materials);

            std::shared_ptr<Rendering::Mesh> GetMesh() { return loadedMesh; }
            std::vector<std::shared_ptr<Rendering::Material>> GetMaterials() { return loadedMaterials; }

            const std::shared_ptr<Rendering::Mesh>& MeshRef() const { return loadedMesh; }
            const std::vector<std::shared_ptr<Rendering::Material>>& MaterialList() const { return loadedMaterials; }

            int GetMaterialsCount() { return loadedMaterials.size(); }

            void AddToPass(const std::string &passName);
            void RemoveFromPass(const std::string &passName);
            const std::vector<std::string>& ExtraPasses() const { return extraPasses; }

            void Destroy() override;

            std::shared_ptr<Component> Clone() const override;
            
            FIELD(Editable, tooltip=The mesh to draw)
            Filesystem::AssetID mesh;

            FIELD(Editable, tooltip=One material per submesh)
            std::vector<Filesystem::AssetID> materials;

            FIELD(Editable, tooltip=Take part in the baked lighting : it blocks and bounces light and gets a lightmap. Turn it off for what moves)
            bool staticLighting = true;

            FIELD(Editable, range=0|2048, tooltip=Lightmap size of this model in texels per side. 0 is worked out from the bake texel density)
            int lightmapResolution = 0;

            // Page in the baked atlas plus the transform from mesh lightmap coords to page coords (uv * xy + zw). Written by a bake, not edited by hand. Negative page means no lightmap.
            int lightmapPage = -1;
            glm::vec4 lightmapScaleOffset = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);

            void ClearLightmap() { lightmapPage = -1; }
            
            DECLARE_DESCRIPTOR(Model)

            void OnFieldChanged(const FieldChangedEvent &event) override;
        
        private:

            std::shared_ptr<Rendering::Mesh> loadedMesh;
            std::vector<std::shared_ptr<Rendering::Material>> loadedMaterials;

            std::vector<std::string> extraPasses;

    };

    void ReloadModelsUsingMesh(Worlds::WorldManager& worlds, const std::string& meshNameInProject);
}