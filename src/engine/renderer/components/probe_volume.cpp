#include "probe_volume.hpp"

#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/features/lighting/probe_manager.hpp"
#include "engine/renderer/components/render_extractor.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/world/components/transform.hpp"
#include "engine/world/actor.hpp"

#include "engine/world/engine.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/assets/assetID.hpp"
#include "engine/assets/project/project.hpp"
#include "engine/assets/asset_database_serializer.hpp"
#include "engine/core/diagnostics/logger.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cctype>
#include <filesystem>

#include "probe_volume.reflection.hpp"

namespace Shard::Engine::Objects::Components{

    ProbeVolume::ProbeVolume(std::shared_ptr<Actor> parent, uint32_t local_id) : Volume(parent, local_id)
    {
        halfExtent = glm::vec3(5.0f, 3.0f, 5.0f);
    }

    glm::vec3 ProbeVolume::GetGridOrigin() const
    {
        glm::vec3 center = parent && parent->transform ? parent->transform->GetWorldPosition() : glm::vec3(0.0f);
        return Rendering::ProbeGridOrigin(center, halfExtent, probeCounts);
    }

    glm::vec3 ProbeVolume::GetGridSpacing() const
    {
        return Rendering::ProbeGridSpacing(halfExtent, probeCounts);
    }

    uint64_t ProbeVolume::GetRenderKey() const
    {
        if(!parent || !parent->world)
            return 0;

        return Rendering::RenderKeyOf(*GetEngineContext()->GetWorldManager(), parent->world, parent->GetComponentIDInWorld(local_id));
    }

    void ProbeVolume::Deserialize(const json componentData)
    {
        // "maxBounces" is ignored on load. Multi-bounce is now a cross-frame feedback loop, and old worlds would pay N times the ray cost for fewer bounces. Not written on next save.

        DeserializeReflectedFields(componentData, false);

        if (componentData.contains("bakedData") && componentData["bakedData"].is_string())
            bakedData = componentData["bakedData"].get<std::string>();
        else
            bakedData.clear();

        DeserializeActive(componentData, false);
    }

    ordered_json ProbeVolume::Serialize()
    {
        ordered_json comp = SerializeReflected("probeVolume");

        if (!bakedData.empty())
            comp["bakedData"] = bakedData;

        return comp;
    }

    std::shared_ptr<Component> ProbeVolume::Clone() const
    {
        std::shared_ptr<ProbeVolume> clone = Object::Create<ProbeVolume>(*this);

        // A copy must not keep the reference, baking either volume would overwrite the file the other still loads
        clone->bakedData.clear();

        return clone;
    }

    namespace {
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
        if (!activated || !parent || !parent->world || !parent->world->IsLoaded())
        {
            DEBUG_WARNING("ProbeVolume : only an active volume in a loaded world can be baked.");
            return false;
        }

        Core::IEngineContext* engine = GetEngineContext();
        const Filesystem::Path resRoot = engine->GetFileManager()->GetProjectResRoot();

        // Re-bake overwrites the volume's own file. First bake writes a new unique file in "probes". The world stores the full path, so later renames do not matter.
        std::string relative = bakedData;
        if (relative.empty())
        {
            const std::string stem = "probes/" + SanitizeForFileName(parent->world->GetName()) + "_" + SanitizeForFileName(parent->GetName());
            relative = stem + Rendering::kProbeBakeExtension;

            for (int n = 2; (resRoot / relative).Exists(); n++)
                relative = stem + "_" + std::to_string(n) + Rendering::kProbeBakeExtension;
        }

        return engine->GetRenderContext()->GetProbeManager()->BeginBake(GetRenderKey(), engine->GetRenderContext()->GetSnapshot(), resRoot / relative);
    }

    void ProbeVolume::OnBakeFinished(const Filesystem::Path& file)
    {
        Core::IEngineContext* engine = GetEngineContext();
        Filesystem::FileManager* fileManager = engine->GetFileManager();

        const std::string nameInProject = fileManager->GetFileInfos(file).nameInProject;
        if (nameInProject.empty())
        {
            DEBUG_ERROR("ProbeVolume : baked to '" + file.full + "', which is outside the project's resources - the world cannot reference it.");
            return;
        }

        // The asset database is only read at project load, so without this the file has no ID and the next world load cannot resolve it
        fileManager->RegisterAsset(file);

        bakedData = nameInProject;

        engine->GetResourcesManager()->Unload(Core::Resources::AssetKind::ProbeBake, bakedData);

        // Written now, not at shutdown, so a crash cannot leave a saved world pointing at a file the database does not know
        if (auto project = engine->GetCurrentProject())
            Serialization::SerializeAssetDataBase(project->GetAssetDatabasePath(), *engine->GetAssetIDManager());

        if (parent && parent->world)
            parent->world->SetDirty(true);

        DEBUG_INFO("ProbeVolume : baked GI probes saved to " + bakedData + " - save the world to keep the reference.");
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
                    Serialization::SerializeAssetDataBase(project->GetAssetDatabasePath(), *engine->GetAssetIDManager());
            }

            engine->GetResourcesManager()->Unload(Core::Resources::AssetKind::ProbeBake, bakedData);
            bakedData.clear();
        }

        if (parent && parent->world)
            parent->world->SetDirty(true);
    }

}
