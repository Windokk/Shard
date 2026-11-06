#pragma once

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/renderer/components/volume.hpp"
#include "engine/renderer/rhi/render_snapshot.hpp"

namespace Shard::Engine::Rendering
{
    class EnvironmentMap;
}

namespace Shard::Engine::Objects::Components
{
    // Baked panorama that glossy surfaces inside the box reflect. One probe per frame, the nearest to the camera (no texture unit left for a second).
    class CLASS() ReflectionProbe : public Volume{
        public:
            ReflectionProbe(std::shared_ptr<Actor> parent, uint32_t local_id);

            void Deserialize(const json componentData) override;
            ordered_json Serialize() override;

            std::shared_ptr<Component> Clone() const override;

            uint64_t GetRenderKey() const;

            std::string bakedData;

            std::shared_ptr<Rendering::EnvironmentMap> GetEnvironment() const { return m_Environment; }

            void RefreshEnvironment();

            bool Bake();

            void ClearBake();

            float GetBakeProgress() const;
            bool IsBaking() const { return GetBakeProgress() >= 0.0f; }

            void CancelBake();

            static void TickAll();

            FIELD(Editable, range=128|4096, tooltip=Width in pixels of the baked picture. The height is half of it)
            int bakeResolution = 1024;

            FIELD(Editable, range=1|4096, tooltip=Samples per pixel of the bake)
            int bakeSamples = 256;

            FIELD(Editable, range=1|16, tooltip=Light bounces of the bake)
            int bakeBounces = 4;

            // Cylinder and cone stand along Y, cone and pyramid narrow toward +Y. Box projection is exact for Box and Sphere only.
            FIELD(Editable, tooltip=Shape of the region the probe covers, inscribed in its box)
            Rendering::ReflectionProbeShape shape = Rendering::ReflectionProbeShape::Box;

            FIELD(Editable, range=0.0f|8.0f, tooltip=Multiplier on the light of the reflection. 1 is as baked)
            float intensity = 1.0f;

            FIELD(Editable, range=0.0f|20.0f, tooltip=Distance inside the edge of the box over which the reflection fades into the sky reflection)
            float blendDistance = 1.0f;

            FIELD(Editable, tooltip=Corrects the reflection for the size of the box, which a room needs. Off for an open area)
            bool boxProjection = true;

            DECLARE_DESCRIPTOR(ReflectionProbe)

        private:
            std::shared_ptr<Rendering::EnvironmentMap> m_Environment;
            std::string m_EnvironmentPath;
    };
}
