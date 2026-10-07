#pragma once

#include "engine/world/components/component.hpp"

#include "engine/renderer/rhi/render_context.hpp"

#include "engine/assets/reflection/attributes.hpp"

#include "engine/renderer/features/lighting/light_manager.hpp"

namespace Shard::Engine::Objects::Components
{
    
    class CLASS() Light : public Component{
        public:
            Light(std::shared_ptr<Actor> parent, uint32_t local_id);

            void SetType(Rendering::LightType type);
            void SetIntensity(float intensity);
            void SetPosition(glm::vec3 postion);
            void SetLightIndex(int index);

            /// @brief Force this light's cached index to match its new slot after a sibling
            /// light was removed and the world's light array shifted (does not touch the renderer
            /// side, which LightManager::RemoveLight() already re-indexed on its own)
            void ReindexTo(int index) { lightIndex = index; }
            void SetCastShadow(bool castShadows);
            void SetColor(COL_RGB color);

            void Activate() override;
            void DeActivate() override;

            void OnTransformChanged(uint8_t changes) override;

            // Directional / Spot light
            void SetDirection(glm::vec3 direction);

            // Spot light
            void SetOuterCutoff(float cutoff);
            void SetInnerCuttoff(float cutoff);

            // Point light / Spot light
            void SetRadius(float radius);
            
            int GetLightIndex() { return lightIndex; }

            void Deserialize(const json componentData) override;

            ordered_json Serialize() override;
            
            Rendering::LightData GetData();
            
            void Destroy() override;

            std::shared_ptr<Component> Clone() const override;

            void OnFieldChanged(const FieldChangedEvent &event) override;

            void UpdateExposedValues();

            FIELD(Editable)
            Rendering::LightType lightType;

            FIELD(Editable)
            float intensity;

            FIELD(Editable)
            float radius;

            FIELD(Editable)
            COL_RGB color;

            FIELD(Editable, range=0.0f|90.0f)
            float outerCutoff;

            FIELD(Editable, range=0.0f|90.0f)
            float innerCutoff;

            FIELD(Editable)
            bool castShadows;

        private:
            int lightIndex = -1;
            std::shared_ptr<Rendering::LightData> lightData;

            DECLARE_DESCRIPTOR(Light)
    };
}