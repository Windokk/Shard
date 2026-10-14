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
            void SetMesh(Filesystem::AssetID meshID);
            void UpdateReferenceInWorld();

            void OnTransformChanged(uint8_t changes) override;
            void SetMaterials(std::vector<std::shared_ptr<Rendering::Material>> &&materials);

            std::shared_ptr<Rendering::Mesh> GetMesh() { return mesh; }
            std::vector<std::shared_ptr<Rendering::Material>> GetMaterials() { return materials; }

            // Without the copies, for the Extract stage which looks at every model every frame
            const std::shared_ptr<Rendering::Mesh>& MeshRef() const { return mesh; }
            const std::vector<std::shared_ptr<Rendering::Material>>& MaterialList() const { return materials; }

            int GetMaterialsCount() { return materials.size(); }

            // The model is drawn by the renderer from the snapshot of the Extract stage (see MeshProxy) : nothing here
            // submits draw commands any more, a change to the mesh, the materials or the transform is picked up by the
            // next extraction.

            // Asks for this model to be drawn into an arbitrary extra pass too, on top of its standing passes - used by
            // the editor to draw only the selected actor into EditorOutlineMaskPass rather than the whole scene.
            void AddToPass(const std::string &passName);
            void RemoveFromPass(const std::string &passName);
            const std::vector<std::string>& ExtraPasses() const { return extraPasses; }

            void Destroy() override;

            std::shared_ptr<Component> Clone() const override;
            
            FIELD(Editable, tooltip=The mesh to draw)
            Filesystem::AssetID meshID;

            FIELD(Editable, tooltip=One material per submesh)
            std::vector<Filesystem::AssetID> materialsID;

            // ---- Baked lighting (see LightmapManager) ----
            // Whether the model is part of the baked lighting : it blocks and bounces the light of a bake, and gets a lightmap
            // of its own (the diffuse light it receives, stored on its surface). Leave it off for what moves : a baked shadow
            // does not follow a moving object, and the lightmap of one that was baked in place stays behind.
            FIELD(Editable, tooltip=Take part in the baked lighting : it blocks and bounces light and gets a lightmap. Turn it off for what moves)
            bool staticLighting = true;

            // The lightmap resolution of this model, in texels per side (0 : worked out from the bake's texel density)
            FIELD(Editable, range=0|2048, tooltip=Lightmap size of this model in texels per side. 0 is worked out from the bake texel density)
            int lightmapResolution = 0;

            // Where the model's lightmap is in the baked atlas : the page, and what turns a coordinate of the mesh's lightmap
            // square into one of the page (uv * xy + zw). Written by a bake and saved with the world, not edited by hand ;
            // a negative page is no lightmap.
            int lightmapPage = -1;
            glm::vec4 lightmapScaleOffset = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);

            /// @brief Drops the model's baked lightmap : it is lit by the probes again
            void ClearLightmap() { lightmapPage = -1; }
            
            DECLARE_DESCRIPTOR(Model)

            void OnFieldChanged(const FieldChangedEvent &event) override;
        
        private:

            std::shared_ptr<Rendering::Mesh> mesh;
            std::vector<std::shared_ptr<Rendering::Material>> materials;

            // Extra passes asked for via AddToPass (e.g. the editor's EditorOutlineMaskPass for the selected actor)
            std::vector<std::string> extraPasses;

    };

    /// @brief Hot reload : a mesh asset was replaced in the cache by a freshly read one. Points every model that uses it at the new one
    /// (the materials slots follow the new geometry, its baked lightmap is dropped : it was laid on the old one).
    void ReloadModelsUsingMesh(Worlds::WorldManager& worlds, const std::string& meshNameInProject);
}