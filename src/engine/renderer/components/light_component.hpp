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
            void SetCastShadow(bool castShadows);
            void SetSourceSize(float sourceSize);
            void SetBakeMode(Rendering::LightBakeMode mode);
            void SetColor(COL_RGB color);

            // Directional / Spot light
            void SetDirection(glm::vec3 direction);

            // Spot light
            void SetOuterCutoff(float cutoff);
            void SetInnerCuttoff(float cutoff);

            // Point light / Spot light
            void SetRadius(float radius);
            
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

            // Size of the emitter, for the soft shadows of the offline renderers (the offline raytrace and the lightmap
            // bake - the real-time shadows keep their own filtering). Point / spot light : radius of the emitting sphere
            // in world units. Directional light : angular radius of the sun disc, in degrees (the real sun is 0.27).
            // 0 : an ideal point / direction, with hard shadows.
            FIELD(Editable, range=0.0f|50.0f)
            float sourceSize;

            // What the light does for baked lighting (see LightBakeMode) : Dynamic, never baked ; Stationary, real-time
            // direct light with its bounced light baked ; Static, baked entirely into the lightmaps.
            FIELD(Editable)
            Rendering::LightBakeMode bakeMode;

        private:
            std::shared_ptr<Rendering::LightData> lightData;

            DECLARE_DESCRIPTOR(Light)
    };
}