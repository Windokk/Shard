#pragma once

#include <string>

#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/renderer/components/volume.hpp"

namespace Shard::Engine::Objects::Components
{
    // DDGI probe grid. Shading picks the smallest active volume containing the point. Axis-aligned, follows the actor's translation only.
    class CLASS() ProbeVolume : public Volume{
        public:
            ProbeVolume(std::shared_ptr<Actor> parent, uint32_t local_id);

            void Deserialize(const json componentData) override;
            ordered_json Serialize() override;

            std::shared_ptr<Component> Clone() const override;

            uint64_t GetRenderKey() const;

            glm::vec3 GetGridOrigin() const;

            glm::vec3 GetGridSpacing() const;

            // Path of the baked probe data. Not cleared on edit: a stale file runs live and a re-bake overwrites it.
            std::string bakedData;

            bool Bake();

            void ClearBake();

            void OnBakeFinished(const Filesystem::Path& file);

            FIELD(Editable, range=1|0, tooltip=Number of probes along each axis of the grid)
            glm::ivec3 probeCounts = glm::ivec3(8, 4, 8);

            // Must be a perfect square, each ray is one texel of the octahedral tile. Rounded down, capped at kMaxRaysPerProbe.
            FIELD(Editable, range=1|256, tooltip=Rays traced per probe and update, a perfect square such as 64 or 256. More is less noisy but costly)
            int raysPerProbe = 64;

            // RTXGI round-robin update: stride N traces 1/N of the probes per frame. Main performance knob.
            FIELD(Editable, range=1|8, tooltip=Update only 1/N of the probes each frame. The main performance knob, large coarse volumes can use 2 to 4)
            int probeUpdateStride = 1;

            FIELD(Editable, range=0.0f|4.0f, tooltip=Multiplier on the bounced light fed back into the probes. 1 is physically correct)
            float indirectIntensity = 1.0f;

            // RTXGI probe relocation, offset <= 0.45 * grid spacing. Toggling it re-zeroes the offsets.
            FIELD(Editable, tooltip=Nudge probes out of the geometry they sit inside, so they still give useful data)
            bool enableRelocation = true;

            DECLARE_DESCRIPTOR(ProbeVolume)
    };
}
