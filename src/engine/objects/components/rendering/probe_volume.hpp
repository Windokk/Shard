#pragma once

#include <string>

#include <glm/glm.hpp>

#include "engine/filesystem/filesystem.hpp"
#include "engine/objects/components/rendering/volume.hpp"

namespace Shard::Engine::Objects::Components
{
    // Places a real-time diffuse GI probe grid in the level (see ProbeManager). Bounding box (halfExtent)
    // and its wireframe gizmo come from Volume - this adds the probe grid resolution/ray count and a
    // second gizmo (small white spheres, one per probe - see RebuildProbeVisualization()). Up to
    // ProbeManager::kMaxProbeVolumes volumes can be simultaneously active (see
    // ProbeManager::AddActiveVolume) - a shading point picks the SMALLEST active volume whose grid
    // actually contains it (see DDGI_PickVolume in lit.frag), so a small, densely-packed local volume
    // automatically overrides a coarser one it's nested inside without any explicit priority field. This
    // is the fix for an object too small for a room-scale grid to resolve at all (e.g. a small prop
    // sitting inside a building-sized probe volume, where the grid spacing is many times the prop's own
    // size) : give it its own small ProbeVolume instead of trying to make the whole level's grid denser.
    // The grid is axis-aligned in world space, centered on this component's actor position - it does not
    // follow the actor's rotation/scale, only its translation.
    class CLASS() ProbeVolume : public Volume{
        public:
            ProbeVolume(std::shared_ptr<Actor> parent, uint32_t local_id);

            void Activate() override;
            void DeActivate() override;
            void Destroy() override;

            void Deserialize(const json componentData) override;
            ordered_json Serialize() override;

            std::shared_ptr<Component> Clone() const override;

            void OnFieldChanged(const FieldChangedEvent &event) override;

            // World-space position of probe (0,0,0) in the grid (the "min corner").
            glm::vec3 GetGridOrigin() const;

            // World-space distance between adjacent probes along each axis.
            glm::vec3 GetGridSpacing() const;

            // ---- Baking (see ProbeManager's "Baked volumes" comment) ----
            // Path in the project (e.g. "probes/sponza_Probes_0.probes") of this volume's baked probe data,
            // referenced by the level file the way a material references a texture : the level's asset
            // prefetcher decodes it on a worker thread while the level loads, and Activate() uploads it so
            // the volume comes up already converged. Empty if the volume has never been baked. It is
            // deliberately NOT cleared when the volume is edited : the file is then just out of date (the
            // next load says so and runs the volume live) and re-baking overwrites it in place.
            std::string bakedData;

            // Bakes this volume into a file under the project's "probes" folder (or, for a re-bake, over
            // the file it already has), asynchronously - see ProbeManager::BeginBake(). Needs the volume to
            // be active in a loaded level. Returns false (logged) if the bake couldn't start.
            bool Bake();

            // Deletes this volume's baked data (file, asset entry, reference) and puts it back on live
            // updates. No-op on the file side if it was never baked.
            void ClearBake();

            // Called by ProbeManager the moment a bake has written `file` : registers it as an asset (so
            // the next level load can resolve it by path), records it in bakedData and flags the level as
            // having unsaved changes - the level file has to be saved for the reference to persist.
            void OnBakeFinished(const Filesystem::Path& file);

            FIELD(Editable)
            glm::ivec3 probeCounts = glm::ivec3(8, 4, 8);

            // Traced per probe per update - must be a perfect square (8x8=64, 16x16=256) since each ray
            // maps directly to one texel of the probe's octahedral irradiance tile (see
            // ProbeManager/probe_trace.comp). Values that aren't a perfect square are rounded down to the
            // nearest one when the grid is (re)built, and the whole thing is capped at
            // ProbeManager::kMaxRaysPerProbe. Cost is linear in this while noise only falls with its
            // square root, so it's rarely the right knob to turn first - see probeUpdateStride.
            FIELD(Editable)
            int raysPerProbe = 64;

            // Round-robin probe update (RTXGI's, see ProbeManager) : a stride of N traces only 1/N of
            // this volume's probes each frame, so each probe refreshes every N frames and the per-frame
            // GPU cost of the whole volume drops by N. THE primary performance knob - a large, coarse
            // room-scale volume can usually sit at 2-4 with no visible difference, since its probes
            // describe light that changes slowly by construction, while a small volume wrapped tightly
            // around a moving object wants 1. Clamped to ProbeManager::kMaxProbeUpdateStride. The
            // temporal blend compensates automatically (see kBaseTemporalHysteresis), so raising this
            // costs responsiveness to a lighting change only through a slightly grainier result, not
            // through a longer settling time.
            FIELD(Editable)
            int probeUpdateStride = 1;

            // Multiplier on the bounce (indirect) term the probes feed back into themselves - RTXGI's
            // "probe irradiance scale". 1.0 is physically correct and the default. Bounce depth itself
            // is NOT a setting any more : light is fed back through the published atlas one hop per
            // frame (see ProbeManager's class comment), so the bounce count converges to effectively
            // unbounded on its own, for the cost of a single trace pass. Raising this slightly (1.2-1.5)
            // exaggerates colour bleed, which can be worth it because an octahedral tile loses a little
            // energy at every hop; lowering it tames a scene whose albedos are high enough that the
            // bounce series takes an uncomfortably long time to settle.
            FIELD(Editable)
            float indirectIntensity = 1.0f;

            // RTXGI "Probe Relocation" : each frame, every probe is nudged by a small bounded offset
            // (<= 0.45 * grid spacing) out of any geometry it sits inside or grazes, so a uniform grid
            // landing probes inside walls/pillars/statues still produces useful data there instead of
            // just switching those probes off (that's probe classification, which always runs). On by
            // default; toggling it re-zeroes accumulated offsets (via ProbeManager::RebuildGrid).
            FIELD(Editable)
            bool enableRelocation = true;

            DECLARE_DESCRIPTOR(ProbeVolume)

        protected:
            void RefreshDebugDrawCommands() override;

            // The grid is translation-only (see class comment) - override so the box wireframe (from
            // Volume) and the probe-marker gizmo below both preview that instead of the actor's full
            // transform, which would show rotation/scale the actual grid doesn't have.
            glm::mat4 GetDebugModelMatrix() const override;

        private:
            // (Re)builds the probe-marker gizmo (small white spheres, one per probe position) - called
            // whenever halfExtent or probeCounts changes, since both affect probe layout.
            void RebuildProbeVisualization();

            Rendering::DebugShape* m_ProbeDebugShape = nullptr;
    };
}
