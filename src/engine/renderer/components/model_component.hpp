#pragma once

#include "engine/world/components/component.hpp"

#include "engine/assets/reflection/attributes.hpp"

#include "engine/renderer/rhi/resources/mesh/mesh.hpp"

namespace Shard::Engine::Rendering{
    class Mesh;
    class Material;
}

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
            
            FIELD(Editable)
            Filesystem::AssetID meshID;

            FIELD(Editable)
            std::vector<Filesystem::AssetID> materialsID;
            
            DECLARE_DESCRIPTOR(Model)

            void OnFieldChanged(const FieldChangedEvent &event) override;
        
        private:

            std::shared_ptr<Rendering::Mesh> mesh;
            std::vector<std::shared_ptr<Rendering::Material>> materials;

            // Extra passes asked for via AddToPass (e.g. the editor's EditorOutlineMaskPass for the selected actor)
            std::vector<std::string> extraPasses;

    };
}