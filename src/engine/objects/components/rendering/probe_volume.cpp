#include "probe_volume.hpp"

#include "engine/rendering/renderer/renderer.hpp"
#include "engine/rendering/lighting/probe_manager.hpp"
#include "engine/rendering/mesh/mesh.hpp"
#include "engine/rendering/debug/debug_shapes.hpp"
#include "engine/objects/components/misc/transform.hpp"
#include "engine/objects/actors/actor.hpp"

#include "engine/core/engine.hpp"
#include "engine/core/resources/resources_manager.hpp"
#include "engine/filesystem/assetID.hpp"
#include "engine/projects/project.hpp"
#include "engine/serialization/assets/asset_database_serializer.hpp"
#include "engine/debugging/logger.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cctype>
#include <filesystem>

#include "probe_volume.reflection.hpp"

namespace Shard::Engine::Objects::Components{

    ProbeVolume::ProbeVolume(std::shared_ptr<Actor> parent, uint32_t local_id) : Volume(parent, local_id)
    {
        // A probe volume defaults to a room-sized box rather than Volume's generic 1x1x1 default.
        halfExtent = glm::vec3(5.0f, 3.0f, 5.0f);
    }

    glm::vec3 ProbeVolume::GetGridOrigin() const
    {
        glm::vec3 center = parent && parent->transform ? parent->transform->GetWorldPosition() : glm::vec3(0.0f);
        // Inset by half a cell (see GetGridSpacing()) so the outermost layer of probes sits inside the
        // volume's bounding box instead of exactly on its surface - a probe volume is typically sized
        // flush against a room's walls, and a probe placed right on/embedded in a wall sees it fill
        // almost its entire hemisphere, turning the octahedral tile's coarse angular resolution into a
        // visible faceted (diamond-shaped, from the octahedral projection) pattern directly on that wall.
        return center - halfExtent + GetGridSpacing() * 0.5f;
    }

    glm::vec3 ProbeVolume::GetGridSpacing() const
    {
        glm::ivec3 divisions = glm::max(probeCounts, glm::ivec3(1));
        return (halfExtent * 2.0f) / glm::vec3(divisions);
    }

    void ProbeVolume::RebuildProbeVisualization()
    {
        if (!activated || !parent)
            return;

        glm::ivec3 counts = glm::max(probeCounts, glm::ivec3(1));
        glm::vec3 spacing = GetGridSpacing();
        glm::vec3 localOrigin = -halfExtent + spacing * 0.5f; // relative to the actor's position - see GetGridOrigin()

        std::vector<glm::vec3> centers;
        centers.reserve((size_t)counts.x * (size_t)counts.y * (size_t)counts.z);
        for (int z = 0; z < counts.z; z++)
            for (int y = 0; y < counts.y; y++)
                for (int x = 0; x < counts.x; x++)
                    centers.push_back(localOrigin + spacing * glm::vec3((float)x, (float)y, (float)z));

        Filesystem::AssetID previousDebugMeshID;
        if (m_ProbeDebugShape && m_ProbeDebugShape->m_Mesh)
            previousDebugMeshID = m_ProbeDebugShape->m_Mesh->GetAssetID();

        delete m_ProbeDebugShape;
        m_ProbeDebugShape = new Rendering::DebugMultiSphere(centers, 0.08f, COL_RGBA(1.0f, 1.0f, 1.0f, 1.0f));

        if (previousDebugMeshID.GetAsInt() != 0)
            m_ProbeDebugShape->m_Mesh->SetAssetID(previousDebugMeshID);
        else
            m_ProbeDebugShape->m_Mesh->SetAssetID(GetEngineContext()->GetAssetIDManager()->GenerateNewID());

        RefreshDebugDrawCommands();
    }

    glm::mat4 ProbeVolume::GetDebugModelMatrix() const
    {
        return glm::translate(glm::mat4(1.0f), parent->transform->GetWorldPosition());
    }

    void ProbeVolume::RefreshDebugDrawCommands()
    {
        Volume::RefreshDebugDrawCommands();

        if (!m_ProbeDebugShape || !m_ProbeDebugShape->m_Mesh || !parent || !parent->level || !parent->level->IsLoaded())
            return;

        Rendering::DrawCommand cmd = {};

        cmd.boundsMax = m_ProbeDebugShape->m_Mesh->GetBoundsMax();
        cmd.boundsMin = m_ProbeDebugShape->m_Mesh->GetBoundsMin();
        cmd.indexCount = m_ProbeDebugShape->m_Mesh->GetIndexCount();
        cmd.indexOffset = 0;
        cmd.material = GetEngineContext()->GetRenderer()->GetDebugMaterial();
        cmd.mesh = m_ProbeDebugShape->m_Mesh;
        cmd.modelID = parent->GetComponentIDInLevel(local_id);
        cmd.modelMatrix = GetDebugModelMatrix();
        cmd.objectID = parent->GetID().GetAsInt();
        cmd.vertexCount = m_ProbeDebugShape->m_Mesh->GetVertexCount();

        // Its own pass (see Renderer::Init()), not "ForwardPass" like Volume's own box wireframe - lets
        // the editor hide just these markers (RenderPass::enabled) without touching ProbeVolume's
        // activation state, which must keep driving GI regardless of whether the markers are shown.
        GetEngineContext()->GetRenderer()->AddOrUpdateCommands({cmd}, {"ProbeGizmoPass"}, false);
    }

    void ProbeVolume::Activate()
    {
        RebuildProbeVisualization();
        Volume::Activate();

        if(parent && parent->level && parent->level->IsLoaded())
        {
            auto probeManager = GetEngineContext()->GetRenderer()->GetProbeManager();
            auto* resources = GetEngineContext()->GetResourcesManager();

            // Normally the level's asset prefetcher already decoded the bake on a worker thread and it is
            // waiting in the resource cache - this only reads the file itself if it isn't (a volume
            // activated from the editor after the level loaded, ...).
            std::shared_ptr<const Rendering::ProbeBakeData> baked;
            if(!bakedData.empty())
                baked = resources->GetProbeBake(bakedData);

            if(!probeManager->AddActiveVolume(this, baked))
            {
                // Live volume (never baked, or its bake is out of date) : it traces against a scene, which
                // is the expensive part a baked volume gets to skip entirely.
                probeManager->RebuildScene(parent->level);
            }

            // Uploaded (or refused) - either way the CPU copy has served its purpose.
            if(!bakedData.empty())
                resources->UnloadProbeBake(bakedData);
        }
    }

    void ProbeVolume::DeActivate()
    {
        Volume::DeActivate();

        if(parent && parent->level && parent->level->IsLoaded())
            GetEngineContext()->GetRenderer()->GetProbeManager()->RemoveActiveVolume(this);
    }

    void ProbeVolume::Destroy()
    {
        if(parent && parent->level && parent->level->IsLoaded())
            GetEngineContext()->GetRenderer()->GetProbeManager()->RemoveActiveVolume(this);

        if (m_ProbeDebugShape && m_ProbeDebugShape->m_Mesh && parent)
        {
            uint64_t cmdID = Rendering::MakeCommandID(m_ProbeDebugShape->m_Mesh->GetAssetID().GetAsInt(), parent->GetComponentIDInLevel(local_id), 0);
            GetEngineContext()->GetRenderer()->RemoveCommands({cmdID}, {"ProbeGizmoPass"}, false);
        }
        delete m_ProbeDebugShape;
        m_ProbeDebugShape = nullptr;

        Volume::Destroy();
    }

    void ProbeVolume::OnFieldChanged(const FieldChangedEvent &event)
    {
        std::string name = event.field->name;

        if (name == "halfExtent")
        {
            Volume::OnFieldChanged(event); // rebuilds the box wireframe
            RebuildProbeVisualization();   // halfExtent also changes probe spacing - regenerate the markers
        }
        else if (name == "probeCounts")
        {
            RebuildProbeVisualization();
        }

        // enableRelocation only needs the grid rebuilt so accumulated relocation offsets are zeroed
        // (RebuildGrid re-inits probeStateBuffer) - the per-frame dispatch is already gated on the flag.
        if (name == "halfExtent" || name == "probeCounts" || name == "raysPerProbe" || name == "enableRelocation")
        {
            if(parent && parent->level && parent->level->IsLoaded())
            {
                auto probeManager = GetEngineContext()->GetRenderer()->GetProbeManager();
                const bool wasBaked = probeManager->IsVolumeBaked(this);

                // Any of these fields invalidates baked data - it was traced on the old grid - so the
                // rebuild always leaves the volume live (see ProbeManager::RebuildGrid()). bakedData is
                // left alone : the file is now merely out of date, and the next bake overwrites it.
                probeManager->RebuildGrid(this);

                // A baked volume never needed a scene, so it may have none. A live one does.
                if(wasBaked)
                    probeManager->RebuildScene(parent->level);
            }
        }
    }

    void ProbeVolume::Deserialize(const json componentData)
    {
        auto getFloat = [&](const json& obj, const char* key, float fallback) -> float
        {
            if (!obj.contains(key) || !obj[key].is_number())
                return fallback;
            return obj[key].get<float>();
        };

        auto getInt = [&](const json& obj, const char* key, int fallback) -> int
        {
            if (!obj.contains(key) || !obj[key].is_number_integer())
                return fallback;
            return obj[key].get<int>();
        };

        if (componentData.contains("halfExtent") && componentData["halfExtent"].is_object())
        {
            const auto& e = componentData["halfExtent"];
            halfExtent = glm::vec3(getFloat(e, "x", 5.0f), getFloat(e, "y", 3.0f), getFloat(e, "z", 5.0f));
        }

        if (componentData.contains("probeCounts") && componentData["probeCounts"].is_object())
        {
            const auto& c = componentData["probeCounts"];
            probeCounts = glm::ivec3(getInt(c, "x", 8), getInt(c, "y", 4), getInt(c, "z", 8));
        }

        raysPerProbe = getInt(componentData, "raysPerProbe", 64);
        probeUpdateStride = getInt(componentData, "probeUpdateStride", 1);
        indirectIntensity = getFloat(componentData, "indirectIntensity", 1.0f);

        // "maxBounces" is deliberately not read any more : bounce depth stopped being a setting when
        // multi-bounce moved to a cross-frame feedback loop (see ProbeManager's class comment), and a
        // level authored before that change would otherwise keep asking for N times the ray cost to get
        // FEWER bounces than it now gets for free. Ignoring the old key silently is the right migration
        // - it simply stops being written on the next save.

        if (componentData.contains("enableRelocation") && componentData["enableRelocation"].is_boolean())
            enableRelocation = componentData["enableRelocation"].get<bool>();

        // Set before Activate() below, which is what consumes it.
        if (componentData.contains("bakedData") && componentData["bakedData"].is_string())
            bakedData = componentData["bakedData"].get<std::string>();
        else
            bakedData.clear();

        if (componentData.contains("active") && componentData["active"].is_boolean() && componentData["active"].get<bool>())
            Activate();
        else
            DeActivate();
    }

    ordered_json ProbeVolume::Serialize()
    {
        ordered_json comp;

        comp["type"] = "probeVolume";
        comp["active"] = activated;

        comp["halfExtent"]["x"] = halfExtent.x;
        comp["halfExtent"]["y"] = halfExtent.y;
        comp["halfExtent"]["z"] = halfExtent.z;

        comp["probeCounts"]["x"] = probeCounts.x;
        comp["probeCounts"]["y"] = probeCounts.y;
        comp["probeCounts"]["z"] = probeCounts.z;

        comp["raysPerProbe"] = raysPerProbe;
        comp["probeUpdateStride"] = probeUpdateStride;
        comp["indirectIntensity"] = indirectIntensity;
        comp["enableRelocation"] = enableRelocation;

        if (!bakedData.empty())
            comp["bakedData"] = bakedData;

        return comp;
    }

    std::shared_ptr<Component> ProbeVolume::Clone() const
    {
        std::shared_ptr<ProbeVolume> clone = Object::Create<ProbeVolume>(*this);

        // A copy is a different volume : if it kept the reference, baking either of the two would
        // overwrite the file the other one is still loading.
        clone->bakedData.clear();

        return clone;
    }

    namespace {
        // Keeps a level/actor name usable as part of a file name on every platform.
        std::string SanitizeForFileName(const std::string& name)
        {
            std::string out;
            out.reserve(name.size());
            for (unsigned char c : name)
                out.push_back((std::isalnum(c) || c == '-' || c == '_') ? (char)c : '_');
            return out.empty() ? std::string("unnamed") : out;
        }
    }

    bool ProbeVolume::Bake()
    {
        if (!activated || !parent || !parent->level || !parent->level->IsLoaded())
        {
            DEBUG_WARNING("ProbeVolume : only an active volume in a loaded level can be baked.");
            return false;
        }

        Core::IEngineContext* engine = GetEngineContext();
        const Filesystem::Path resRoot = engine->GetFileManager()->GetProjectResRoot();

        // Re-bake : overwrite this volume's own file. First bake : a new file in the project's "probes"
        // folder. Its name only has to be unique NOW - once written, the level file stores the full path,
        // so it doesn't matter that actor names (or the level's) can change later.
        std::string relative = bakedData;
        if (relative.empty())
        {
            const std::string stem = "probes/" + SanitizeForFileName(parent->level->GetName()) + "_" + SanitizeForFileName(parent->GetName());
            relative = stem + Rendering::kProbeBakeExtension;

            for (int n = 2; (resRoot / relative).Exists(); n++)
                relative = stem + "_" + std::to_string(n) + Rendering::kProbeBakeExtension;
        }

        return engine->GetRenderer()->GetProbeManager()->BeginBake(this, parent->level, resRoot / relative);
    }

    void ProbeVolume::OnBakeFinished(const Filesystem::Path& file)
    {
        Core::IEngineContext* engine = GetEngineContext();
        Filesystem::FileManager* fileManager = engine->GetFileManager();

        const std::string nameInProject = fileManager->GetFileInfos(file).nameInProject;
        if (nameInProject.empty())
        {
            DEBUG_ERROR("ProbeVolume : baked to '" + file.full + "', which is outside the project's resources - the level cannot reference it.");
            return;
        }

        // The asset database is otherwise only read at project load, so without this the file has no ID
        // and the next level load couldn't resolve its path.
        fileManager->RegisterAsset(file);

        bakedData = nameInProject;

        // A previous bake of this volume may still be cached (CPU side) from an earlier load.
        engine->GetResourcesManager()->UnloadProbeBake(bakedData);

        // Written straight away rather than at project shutdown, so a crash between here and then can't
        // leave a saved level pointing at a file the database has never heard of.
        if (auto project = engine->GetCurrentProject())
            Serialization::SerializeAssetDataBase(project->GetAssetDatabasePath());

        if (parent && parent->level)
            parent->level->SetDirty(true);

        DEBUG_INFO("ProbeVolume : baked GI probes saved to " + bakedData + " - save the level to keep the reference.");
    }

    void ProbeVolume::ClearBake()
    {
        Core::IEngineContext* engine = GetEngineContext();

        if (!bakedData.empty())
        {
            Filesystem::AssetIDManager* assetManager = engine->GetAssetIDManager();

            std::error_code ec;
            std::filesystem::remove((engine->GetFileManager()->GetProjectResRoot() / bakedData).full, ec);

            Filesystem::AssetID id = assetManager->GetIDFromNameInProject(bakedData);
            std::shared_ptr<Filesystem::AssetInfos> info = assetManager->GetAssetFromID(id);
            if (info && info->baseInfos.nameInProject == bakedData)
            {
                assetManager->DestroyID(id);

                if (auto project = engine->GetCurrentProject())
                    Serialization::SerializeAssetDataBase(project->GetAssetDatabasePath());
            }

            engine->GetResourcesManager()->UnloadProbeBake(bakedData);
            bakedData.clear();
        }

        if (parent && parent->level)
        {
            parent->level->SetDirty(true);

            // A baked volume was running without a scene - back to live means it needs one again. (A
            // volume that was merely out of date is already live and already has its scene.)
            if (activated && parent->level->IsLoaded())
            {
                auto probeManager = engine->GetRenderer()->GetProbeManager();
                if (probeManager->IsVolumeBaked(this))
                {
                    probeManager->RebuildGrid(this);
                    probeManager->RebuildScene(parent->level);
                }
            }
        }
    }

}
