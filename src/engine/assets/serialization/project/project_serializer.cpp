#include "project_serializer.hpp"

#include "config.h"

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
                // An error, not a fatal one : Revert reads the file back while the editor runs
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
                    renderingSettings.msaaSamples = std::clamp<uint32_t>(rendering["msaaSamples"], 2u, 16u);

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

            Projects::TagSettings tagSettings;

            // "tags" : { "Enemy.Boss" : { "comment" : "..." } }, a plain array of names is accepted too
            if(data.contains("tags")){
                const json& tags = data["tags"];
                if(tags.is_object()){
                    for(auto& [tagName, tag] : tags.items())
                        tagSettings.tags[tagName] = (tag.is_object() && tag.contains("comment") && tag["comment"].is_string()) ? tag["comment"].get<std::string>() : std::string();
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

            project = std::make_shared<Projects::Project>(path.GetFilename(false), projectRoot, projectResPath, pluginsPath, buildSettings, assetDatabasePath);

            if(data.contains("plugins") && data["plugins"].is_array())
                project->SetPluginsConfig(data["plugins"]);

            *project->GetPhysicsSettings() = physicsSettings;
            *project->GetRenderingSettings() = renderingSettings;
            *project->GetTagSettings() = tagSettings;

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

        data["rendering"]["occlusionCulling"] = pro->GetRenderingSettings()->occlusionCulling;
        data["rendering"]["msaaSamples"] = pro->GetRenderingSettings()->msaaSamples;
        data["rendering"]["renderPipeline"] = pro->GetRenderingSettings()->renderPipeline;
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

        // Only when there are some : a project without tags keeps the file it had
        const Projects::TagSettings* tags = pro->GetTagSettings();
        if(!tags->tags.empty()){
            ordered_json out = ordered_json::object();
            for(const auto& [tagName, comment] : tags->tags){
                ordered_json tag = ordered_json::object();
                if(!comment.empty())
                    tag["comment"] = comment;
                out[tagName] = tag;
            }
            data["tags"] = out;
        }
        if(!tags->redirects.empty())
            data["tagRedirects"] = tags->redirects;

        // The editor preferences are the user's, not the project's : the editor keeps them in its own file
        return data;
    }
}