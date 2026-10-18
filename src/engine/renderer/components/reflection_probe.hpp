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
    // A captured picture of what surrounds a point of the world, which glossy surfaces inside its box reflect : the sky alone says
    // nothing of the walls of a room, so without one an indoor surface reflects a sky it cannot see (or nothing at all).
    //
    // The capture is a bake : Bake() path traces the whole sphere around the actor with the offline raytracer (so the picture holds the
    // lights, the lightmaps' worth of bounces and the sky as the world has them), saves it as an equirectangular .hdr in the project and
    // references it, the way a ProbeVolume references its baked probes. The picture is then loaded like a skybox : the cubemap, and the
    // prefiltered cubemap the lighting samples by roughness (see EnvironmentMap).
    //
    // A surface inside the box of a probe takes its reflection from it, faded against the sky's over `blendDistance` from the edge of the box,
    // with the box projection (the lookup direction is corrected for the box, so the reflection of a wall lands where the wall is and not
    // infinitely far) unless `boxProjection` is off. One probe is used at a time, for the whole frame : the one nearest to the camera (the
    // smaller box when the camera is inside several) - the lighting shader has no texture unit left for a second. The box is centered on the actor,
    // which is also where the picture is taken from, and does not follow its rotation or scale.
    class CLASS() ReflectionProbe : public Volume{
        public:
            ReflectionProbe(std::shared_ptr<Actor> parent, uint32_t local_id);

            void Deserialize(const json componentData) override;
            ordered_json Serialize() override;

            std::shared_ptr<Component> Clone() const override;

            // The key the renderer knows this probe by (see ReflectionProbeProxy)
            uint64_t GetRenderKey() const;

            // Path in the project (e.g. "reflection_probes/sponza_Probe.hdr") of the baked picture, referenced by the world file. Empty if the
            // probe was never baked : it then reflects nothing and the sky stays what surfaces reflect. Not cleared when the probe is
            // edited : the picture is then just out of date, and re-baking overwrites it in place.
            std::string bakedData;

            // The environment map made of the baked picture, loaded (once per path) by the engine's update, never by the Extract stage.
            // Null while the probe has no picture or it could not be loaded.
            std::shared_ptr<Rendering::EnvironmentMap> GetEnvironment() const { return m_Environment; }

            // Loads the picture of `bakedData` if it is not the one already held. Main thread.
            void RefreshEnvironment();

            // Starts baking this probe (asynchronously, a few steps per engine update : see TickBakes). Needs the probe to be active in a loaded world,
            // and no other bake running. Returns false (logged) if the bake could not start.
            bool Bake();

            // Deletes the baked picture (file, asset entry, reference) : the probe reflects nothing again
            void ClearBake();

            // 0 to 1 while this probe is being baked, a negative number when it is not
            float GetBakeProgress() const;
            bool IsBaking() const { return GetBakeProgress() >= 0.0f; }

            // Stops the bake of this probe, if it is running
            void CancelBake();

            // What the engine does for every probe of the loaded worlds each update : loads the pictures that changed, advances the running bake
            static void TickAll();

            // Width in pixels of the baked picture (its height is half). 1024 is plenty : it is turned into a 512 pixel cubemap
            FIELD(Editable, range=128|4096, tooltip=Width in pixels of the baked picture. The height is half of it)
            int bakeResolution = 1024;

            // Rays per pixel. The picture is blurred by the roughness filter anyway, so a few hundred are plenty
            FIELD(Editable, range=1|4096, tooltip=Samples per pixel of the bake)
            int bakeSamples = 256;

            FIELD(Editable, range=1|16, tooltip=Light bounces of the bake)
            int bakeBounces = 4;

            // The shape of the region the probe covers, inscribed in the box (`halfExtent`) : a surface outside of it does not reflect the probe.
            // Cylinder and cone stand along Y, cone and pyramid narrow toward +Y. The box projection is exact for Box and Sphere, and done
            // against the bounding box for the others.
            FIELD(Editable, tooltip=Shape of the region the probe covers, inscribed in its box)
            Rendering::ReflectionProbeShape shape = Rendering::ReflectionProbeShape::Box;

            // How strongly the reflection of this probe shows. 1 is the light the picture holds
            FIELD(Editable, range=0.0f|8.0f, tooltip=Multiplier on the light of the reflection. 1 is as baked)
            float intensity = 1.0f;

            // Distance from the edge of the box, inside it, over which the reflection fades to the sky's. 0 : it stops sharply at the edge
            FIELD(Editable, range=0.0f|20.0f, tooltip=Distance inside the edge of the box over which the reflection fades into the sky reflection)
            float blendDistance = 1.0f;

            // Corrects the direction of the lookup for the shape of the box : right for a probe that fills a room, wrong for one meant to stand for
            // something infinitely far (a sky) - turn it off for the latter
            FIELD(Editable, tooltip=Corrects the reflection for the size of the box, which a room needs. Off for an open area)
            bool boxProjection = true;

            DECLARE_DESCRIPTOR(ReflectionProbe)

        private:
            std::shared_ptr<Rendering::EnvironmentMap> m_Environment;
            std::string m_EnvironmentPath;      // the `bakedData` m_Environment was loaded from
    };
}
