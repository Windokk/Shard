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

            FIELD(Editable, tooltip=Directional, point or spot)
            Rendering::LightType lightType;

            FIELD(Editable, tooltip=Brightness of the light)
            float intensity;

            FIELD(Editable, tooltip=Distance at which the light stops having an effect, point and spot lights)
            float radius;

            FIELD(Editable, tooltip=Color of the emitted light)
            COL_RGB color;

            FIELD(Editable, range=0.0f|90.0f, tooltip=Angle in degrees where the cone fades out completely, spot light only)
            float outerCutoff;

            FIELD(Editable, range=0.0f|90.0f, tooltip=Angle in degrees of the full brightness cone, spot light only)
            float innerCutoff;

            FIELD(Editable, tooltip=Objects block this light and cast shadows)
            bool castShadows;

            // Size of the emitter, for the soft shadows of the offline renderers (the offline raytrace and the lightmap
            // bake - the real-time shadows keep their own filtering). Point / spot light : radius of the emitting sphere
            // in world units. Directional light : angular radius of the sun disc, in degrees (the real sun is 0.27).
            // 0 : an ideal point / direction, with hard shadows.
            FIELD(Editable, range=0.0f|50.0f, tooltip=Size of the emitter for the soft shadows of baking and raytracing. Radius for point and spot lights, angular radius in degrees for a directional one. 0 gives hard shadows)
            float sourceSize;

            // What the light does for baked lighting (see LightBakeMode) : Dynamic, never baked ; Stationary, real-time
            // direct light with its bounced light baked ; Static, baked entirely into the lightmaps.
            FIELD(Editable, tooltip=Dynamic is never baked. Stationary has real-time direct light with its bounced light baked. Static is baked entirely into the lightmaps)
            Rendering::LightBakeMode bakeMode;

        private:
            std::shared_ptr<Rendering::LightData> lightData;

            DECLARE_DESCRIPTOR(Light)
    };
}