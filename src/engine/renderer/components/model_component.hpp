#pragma once

#include "engine/world/components/component.hpp"

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
            void UpdateReferenceInLevel();
            void SetMaterials(std::vector<std::shared_ptr<Rendering::Material>> &&materials);

            std::shared_ptr<Rendering::Mesh> GetMesh() { return mesh; }
            std::vector<std::shared_ptr<Rendering::Material>> GetMaterials() { return materials; }

            int GetMaterialsCount() { return materials.size(); }

            void Update();
            void RemoveFromDrawList();

            // Registers/unregisters this model's draw commands with an arbitrary extra pass, without
            // touching its standing ForwardPass/SSAODepthNormalPass registration - used by the editor
            // to draw only the selected actor into EditorOutlineMaskPass rather than the whole scene.
            void AddToPass(const std::string &passName);
            void RemoveFromPass(const std::string &passName);

            void Activate() override;
            void DeActivate() override;

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

            // Extra passes registered via AddToPass (e.g. the editor's EditorOutlineMaskPass for the
            // selected actor) - tracked so a mesh swap can drop the old mesh's commands from them and
            // re-register the new mesh's, instead of leaving the old mesh's commands orphaned in the pass.
            std::vector<std::string> extraPasses;

    };
}