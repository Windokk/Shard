#include "project_serializer.hpp"
#include "engine/assets/project/quality.hpp"
#include "engine/assets/serialization/collision/collision_json.hpp"

#include <filesystem>

#include "config.h"

#include "engine/assets/pak/pak.hpp"
#include "engine/core/diagnostics/logger.hpp"

using namespace nlohmann;

namespace Shard::Engine::Serialization{

    std::shared_ptr<Projects::Project> DeserializeProject(const Filesystem::Path path)
    {
        if(!path.Exists()){
            DEBUG_ERROR("Project at path : \"" + path.full +"\" doesn't exist !");
            return nullptr;
        }

        std::string src = path.ReadFile();

        try {
            json data = json::parse(src);

            int projectMajorVersion = -1;
            int projectMinorVersion = -1;
            int projectPatchVersion = -1;

            if(data.contains("versionMajor") && data["versionMajor"].is_number_integer()){
                projectMajorVersion = data["versionMajor"];
            }
            if(data.contains("versionMinor") && data["versionMinor"].is_number_integer()){
                projectMinorVersion = data["versionMinor"];
            }
            if(data.contains("versionPatch") && data["versionPatch"].is_number_integer()){
                projectPatchVersion = data["versionPatch"];
            }
            if(projectMajorVersion == -1 || projectMinorVersion == -1 || projectPatchVersion == -1){
                // Not fatal: Revert reads the file back while the editor runs
                DEBUG_ERROR("No version specified/Incorrect version format for project: "+path.full);
                return nullptr;
            }
            else if(projectMajorVersion != VERSION_MAJOR || projectMinorVersion != VERSION_MINOR || projectPatchVersion != VERSION_PATCH){
                DEBUG_ERROR("Version incompatibility found between engine and project : "+path.full+ "\nProject version : "
                            + std::to_string(projectMajorVersion)+"."+std::to_string(projectMinorVersion)+"."+std::to_string(projectPatchVersion)+" Engine version : "+std::to_string(VERSION_MAJOR)+"."+std::to_string(VERSION_MINOR)+"."+std::to_string(VERSION_PATCH));
                return nullptr;
            }
            
            std::shared_ptr<Projects::Project> project = nullptr;

            Filesystem::Path projectResPath;
            Filesystem::Path projectRoot;
            Filesystem::Path pluginsPath;
            Filesystem::Path assetDatabasePath;

            projectRoot = Filesystem::Path(path.GetParent());

            if(data.contains("projectResources") && data["projectResources"].is_string()){
                projectResPath = Filesystem::Path::Normalize(Filesystem::Path(projectRoot / data["projectResources"]).full);

                // A cooked project (ShardCooker project) ships its resources as one pak : unpacked once, then read like a folder
                const std::string pakFile = projectResPath.GetNativePath() + Pak::kExtension;
                if(!std::filesystem::is_directory(projectResPath.GetNativePath()) && std::filesystem::is_regular_file(pakFile)){
                    std::string pakError;
                    const std::string cache = (std::filesystem::temp_directory_path() / "shard-project-cache").string();
                    const std::string folder = Pak::UnpackCached(pakFile, cache, &pakError);
                    if(folder.empty()){
                        DEBUG_ERROR("Cannot open the cooked resources \"" + pakFile + "\" : " + pakError);
                        return nullptr;
                    }
                    DEBUG_INFO("Cooked project : resources unpacked to " + folder);
                    projectResPath = Filesystem::Path::Normalize(folder);
                }
            }
            else{
                DEBUG_FATAL("No project resources specified for project: "+path.full);
            }
            if(data.contains("assetDatabase") && data["assetDatabase"].is_string()){
                assetDatabasePath = Filesystem::Path::Normalize(Filesystem::Path(projectRoot / data["assetDatabase"]).full);
            }
            else{
                DEBUG_FATAL("No asset database specified for project: "+path.full);
            }

            if(data.contains("pluginsFolder") && data["pluginsFolder"].is_string()){
                pluginsPath = Filesystem::Path::Normalize(Filesystem::Path(projectRoot / data["pluginsFolder"]).full);
            }
            else{
                DEBUG_WARNING("No plugins folder specified for project: "+path.full);
            }
            
            Projects::BuildSettings buildSettings;

            if(data.contains("buildSettings") && data["buildSettings"].is_array()){
                for(auto world : data["buildSettings"]){
                    if(!world.is_string())
                        continue;

                    buildSettings.AddToBuildSettings(Filesystem::Path(world));
                }
            }
            else{
                DEBUG_INFO("No build settings for project: "+path.full);
            }

            Projects::PhysicsSettings physicsSettings;

            if(data.contains("physics") && data["physics"].is_object()){
                const json& physics = data["physics"];

                if(physics.contains("gravity") && physics["gravity"].is_object()){
                    const json& gravity = physics["gravity"];
                    if(gravity.contains("x") && gravity["x"].is_number())
                        physicsSettings.gravity.x = gravity["x"];
                    if(gravity.contains("y") && gravity["y"].is_number())
                        physicsSettings.gravity.y = gravity["y"];
                    if(gravity.contains("z") && gravity["z"].is_number())
                        physicsSettings.gravity.z = gravity["z"];
                }

                if(physics.contains("fixedTimeStep") && physics["fixedTimeStep"].is_number()){
                    float fixedTimeStep = physics["fixedTimeStep"];
                    if(fixedTimeStep > 0.0f)
                        physicsSettings.fixedTimeStep = fixedTimeStep;
                    else
                        DEBUG_ERROR("Project physics fixedTimeStep must be greater than 0, keeping the default");
                }

                if(physics.contains("maxAccumulatedTime") && physics["maxAccumulatedTime"].is_number()){
                    float maxAccumulatedTime = physics["maxAccumulatedTime"];
                    if(maxAccumulatedTime >= physicsSettings.fixedTimeStep)
                        physicsSettings.maxAccumulatedTime = maxAccumulatedTime;
                    else
                        DEBUG_ERROR("Project physics maxAccumulatedTime must be at least one fixedTimeStep, keeping the default");
                }

                if(physics.contains("velocitySteps") && physics["velocitySteps"].is_number_unsigned())
                    physicsSettings.velocitySteps = std::clamp<uint32_t>(physics["velocitySteps"], 1u, 64u);

                if(physics.contains("positionSteps") && physics["positionSteps"].is_number_unsigned())
                    physicsSettings.positionSteps = std::clamp<uint32_t>(physics["positionSteps"], 1u, 64u);

                if(physics.contains("collisionSteps") && physics["collisionSteps"].is_number_unsigned())
                    physicsSettings.collisionSteps = std::clamp<uint32_t>(physics["collisionSteps"], 1u, 8u);
            }
            else{
                DEBUG_INFO("No physics settings for project: "+path.full+", using the defaults");
            }

            Projects::RenderingSettings renderingSettings;

            if(data.contains("rendering") && data["rendering"].is_object()){
                const json& rendering = data["rendering"];

                if(rendering.contains("occlusionCulling") && rendering["occlusionCulling"].is_boolean())
                    renderingSettings.occlusionCulling = rendering["occlusionCulling"];

                if(rendering.contains("msaaSamples") && rendering["msaaSamples"].is_number_unsigned())
                    renderingSettings.msaaSamples = std::clamp<uint32_t>(rendering["msaaSamples"], 1u, 16u);

                if(rendering.contains("ssao") && rendering["ssao"].is_boolean())
                    renderingSettings.ssao = rendering["ssao"];

                if(rendering.contains("ssr") && rendering["ssr"].is_boolean())
                    renderingSettings.ssr = rendering["ssr"];

                if(rendering.contains("maxAnisotropy") && rendering["maxAnisotropy"].is_number_unsigned())
                    renderingSettings.maxAnisotropy = std::clamp<uint32_t>(rendering["maxAnisotropy"], 1u, 16u);

                if(rendering.contains("renderPipeline") && rendering["renderPipeline"].is_string())
                    renderingSettings.renderPipeline = rendering["renderPipeline"].get<std::string>();

                if(rendering.contains("shadows") && rendering["shadows"].is_object()){
                    const json& shadows = rendering["shadows"];
                    Projects::ShadowSettings& out = renderingSettings.shadows;

                    auto readBool = [&](const char* key, bool& value) {
                        if(shadows.contains(key) && shadows[key].is_boolean())
                            value = shadows[key];
                    };
                    auto readFloat = [&](const char* key, float& value, float min, float max) {
                        if(shadows.contains(key) && shadows[key].is_number())
                            value = std::clamp(shadows[key].get<float>(), min, max);
                    };
                    auto readUint = [&](const char* key, uint32_t& value, uint32_t min, uint32_t max) {
                        if(shadows.contains(key) && shadows[key].is_number_unsigned())
                            value = std::clamp<uint32_t>(shadows[key], min, max);
                    };

                    readBool("enabled", out.enabled);
                    readUint("directionalResolution", out.directionalResolution, 256u, 8192u);
                    readFloat("directionalDistance", out.directionalDistance, 0.0f, 100000.0f);
                    if(shadows.contains("cascadeSplits") && shadows["cascadeSplits"].is_array()){
                        out.cascadeSplits.clear();
                        for(const json& split : shadows["cascadeSplits"])
                            if(split.is_number())
                                out.cascadeSplits.push_back(split.get<float>());
                    }
                    if(shadows.contains("cascadeColors") && shadows["cascadeColors"].is_array()){
                        out.cascadeColors.clear();
                        for(const json& color : shadows["cascadeColors"])
                            if(color.is_array() && color.size() == 3)
                                out.cascadeColors.emplace_back(color[0].get<float>(), color[1].get<float>(), color[2].get<float>());
                    }
                    out.SanitizeCascades();
                    readBool("stabilizeCascades", out.stabilizeCascades);
                    readFloat("casterRange", out.casterRange, 1.0f, 100000.0f);
                    readBool("directionalDoubleSided", out.directionalDoubleSided);
                    readBool("directionalConservativeRaster", out.directionalConservativeRaster);
                    readFloat("directionalNormalBias", out.directionalNormalBias, 0.0f, 16.0f);
                    readFloat("directionalDepthBias", out.directionalDepthBias, 0.0f, 16.0f);
                    readFloat("directionalSlopeBias", out.directionalSlopeBias, 0.0f, 4.0f);
                    readUint("directionalFilterRadius", out.directionalFilterRadius, 0u, 4u);
                    readFloat("directionalFilterSpread", out.directionalFilterSpread, 0.1f, 8.0f);
                    readUint("spotResolution", out.spotResolution, 256u, 8192u);
                    readFloat("spotDepthBias", out.spotDepthBias, 0.0f, 0.1f);
                    readFloat("spotMinDepthBias", out.spotMinDepthBias, 0.0f, 0.1f);
                    readUint("spotFilterRadius", out.spotFilterRadius, 0u, 4u);
                    readFloat("spotFilterSpread", out.spotFilterSpread, 0.1f, 8.0f);
                    readUint("pointResolution", out.pointResolution, 256u, 4096u);
                    readFloat("pointDepthBias", out.pointDepthBias, 0.0f, 5.0f);
                    readUint("pointFilterSamples", out.pointFilterSamples, 1u, 20u);
                    readFloat("pointFilterRadius", out.pointFilterRadius, 0.0f, 2.0f);
                }
            }
            else{
                DEBUG_INFO("No rendering settings for project: "+path.full+", using the defaults");
            }

            Projects::QualitySettings qualitySettings;

            if(data.contains("quality") && data["quality"].is_object()){
                const json& quality = data["quality"];
                if(quality.contains("preset") && quality["preset"].is_string()){
                    const std::string preset = quality["preset"].get<std::string>();
                    if(!Projects::ParseQualitySelection(preset, qualitySettings.preset))
                        DEBUG_ERROR("Project quality preset \"" + preset + "\" is not custom, auto, ultra_low, low, medium, high or ultra_high, using custom");
                }
            }

            Projects::TagSettings tagSettings;

            if(data.contains("tags")){
                const json& tags = data["tags"];
                if(tags.is_object()){
                    for(auto& [tagName, tag] : tags.items()){
                        tagSettings.tags[tagName] = (tag.is_object() && tag.contains("comment") && tag["comment"].is_string()) ? tag["comment"].get<std::string>() : std::string();
                        if(tag.is_object() && tag.contains("collision") && tag["collision"].is_boolean() && tag["collision"].get<bool>())
                            tagSettings.collisionTags.insert(tagName);
                    }
                }
                else if(tags.is_array()){
                    for(const json& tag : tags)
                        if(tag.is_string())
                            tagSettings.tags[tag.get<std::string>()] = std::string();
                }
                else
                    DEBUG_ERROR("Project tags must be an object (name : { comment }), ignoring them");
            }

            if(data.contains("tagRedirects") && data["tagRedirects"].is_object()){
                for(auto& [from, to] : data["tagRedirects"].items())
                    if(to.is_string())
                        tagSettings.redirects[from] = to.get<std::string>();
            }

            Projects::CollisionSettings collisionSettings;
            {
                std::vector<std::string> problems;
                if(data.contains("collision_profiles") && data["collision_profiles"].is_object())
                    for(auto& [name, profile] : data["collision_profiles"].items())
                        collisionSettings.profiles.push_back(ProfileFromJson(name, profile, &problems));
                if(data.contains("collision_interactions") && data["collision_interactions"].is_array())
                    for(const json& row : data["collision_interactions"])
                        collisionSettings.interactions.push_back(InteractionFromJson(row, &problems));
                for(const std::string& problem : problems)
                    DEBUG_ERROR("Project collision settings : " + problem);
            }

            project = std::make_shared<Projects::Project>(path.GetFilename(false), projectRoot, projectResPath, pluginsPath, buildSettings, assetDatabasePath);

            if(data.contains("plugins") && data["plugins"].is_array())
                project->SetPluginsConfig(data["plugins"]);

            *project->GetPhysicsSettings() = physicsSettings;
            *project->GetRenderingSettings() = renderingSettings;
            *project->GetQualitySettings() = qualitySettings;
            *project->GetTagSettings() = tagSettings;
            *project->GetCollisionSettings() = collisionSettings;

            project->versionMajor = projectMajorVersion;
            project->versionMinor = projectMinorVersion;
            project->versionPatch = projectPatchVersion;

            project->MarkSaved();

            return project;

        } catch (const json::parse_error& e) {
            DEBUG_ERROR("JSON parse error: " + (std::string)e.what());
            return nullptr;
        }
    }

    void SerializeProject(Projects::Project* pro, const Filesystem::Path path)
    {
        path.WriteFile(ProjectToJson(pro).dump());
    }

    ordered_json ProjectToJson(Projects::Project* pro)
    {
        ordered_json data;

        data["versionMajor"] = pro->versionMajor;
        data["versionMinor"] = pro->versionMinor;
        data["versionPatch"] = pro->versionPatch;

        data["projectResources"] = pro->GetProjectResourcesPath().RelativeTo(pro->GetProjectRoot()).full;
        data["pluginsFolder"] = pro->GetPluginsFolderPath().RelativeTo(pro->GetProjectRoot()).full;
        data["assetDatabase"] = pro->GetAssetDatabasePath().RelativeTo(pro->GetProjectRoot()).full;
        if(pro->GetPluginsConfig().is_array())
            data["plugins"] = pro->GetPluginsConfig();

        for(auto& world : pro->GetBuildSettings()->buildIndex){
            data["buildSettings"].push_back(world.full);
        }
        if(!data.contains("buildSettings"))
            data["buildSettings"] = ordered_json::array();

        Projects::PhysicsSettings* physics = pro->GetPhysicsSettings();

        data["physics"]["gravity"]["x"] = physics->gravity.x;
        data["physics"]["gravity"]["y"] = physics->gravity.y;
        data["physics"]["gravity"]["z"] = physics->gravity.z;
        data["physics"]["fixedTimeStep"] = physics->fixedTimeStep;
        data["physics"]["maxAccumulatedTime"] = physics->maxAccumulatedTime;
        data["physics"]["velocitySteps"] = physics->velocitySteps;
        data["physics"]["positionSteps"] = physics->positionSteps;
        data["physics"]["collisionSteps"] = physics->collisionSteps;

        data["rendering"]["occlusionCulling"] = pro->GetRenderingSettings()->occlusionCulling;
        data["rendering"]["msaaSamples"] = pro->GetRenderingSettings()->msaaSamples;
        data["rendering"]["renderPipeline"] = pro->GetRenderingSettings()->renderPipeline;
        data["rendering"]["ssao"] = pro->GetRenderingSettings()->ssao;
        data["rendering"]["ssr"] = pro->GetRenderingSettings()->ssr;
        data["rendering"]["maxAnisotropy"] = pro->GetRenderingSettings()->maxAnisotropy;
        {
            const Projects::ShadowSettings& shadows = pro->GetRenderingSettings()->shadows;
            json out = json::object();
            out["enabled"] = shadows.enabled;
            out["directionalResolution"] = shadows.directionalResolution;
            out["directionalDistance"] = shadows.directionalDistance;
            out["cascadeSplits"] = shadows.cascadeSplits;
            out["cascadeColors"] = json::array();
            for(const glm::vec3& color : shadows.cascadeColors)
                out["cascadeColors"].push_back({ color.x, color.y, color.z });
            out["stabilizeCascades"] = shadows.stabilizeCascades;
            out["casterRange"] = shadows.casterRange;
            out["directionalDoubleSided"] = shadows.directionalDoubleSided;
            out["directionalConservativeRaster"] = shadows.directionalConservativeRaster;
            out["directionalNormalBias"] = shadows.directionalNormalBias;
            out["directionalDepthBias"] = shadows.directionalDepthBias;
            out["directionalSlopeBias"] = shadows.directionalSlopeBias;
            out["directionalFilterRadius"] = shadows.directionalFilterRadius;
            out["directionalFilterSpread"] = shadows.directionalFilterSpread;
            out["spotResolution"] = shadows.spotResolution;
            out["spotDepthBias"] = shadows.spotDepthBias;
            out["spotMinDepthBias"] = shadows.spotMinDepthBias;
            out["spotFilterRadius"] = shadows.spotFilterRadius;
            out["spotFilterSpread"] = shadows.spotFilterSpread;
            out["pointResolution"] = shadows.pointResolution;
            out["pointDepthBias"] = shadows.pointDepthBias;
            out["pointFilterSamples"] = shadows.pointFilterSamples;
            out["pointFilterRadius"] = shadows.pointFilterRadius;
            data["rendering"]["shadows"] = out;
        }

        data["quality"]["preset"] = Projects::FormatQualitySelection(pro->GetQualitySettings()->preset);

        const Projects::TagSettings* tags = pro->GetTagSettings();
        if(!tags->tags.empty()){
            ordered_json out = ordered_json::object();
            for(const auto& [tagName, comment] : tags->tags){
                ordered_json tag = ordered_json::object();
                if(!comment.empty())
                    tag["comment"] = comment;
                if(tags->collisionTags.count(tagName))
                    tag["collision"] = true;
                out[tagName] = tag;
            }
            data["tags"] = out;
        }

        const Projects::CollisionSettings* collision = pro->GetCollisionSettings();
        if(!collision->profiles.empty()){
            ordered_json out = ordered_json::object();
            for(const Collision::Profile& profile : collision->profiles)
                out[profile.name] = ProfileToJson(profile);
            data["collision_profiles"] = out;
        }
        if(!collision->interactions.empty()){
            ordered_json out = ordered_json::array();
            for(const Collision::InteractionRow& row : collision->interactions)
                out.push_back(InteractionToJson(row));
            data["collision_interactions"] = out;
        }
        if(!tags->redirects.empty())
            data["tagRedirects"] = tags->redirects;

        return data;
    }
}