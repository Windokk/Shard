#include "project_serializer.hpp"

#include "config.h"

#include "engine/debugging/logger.hpp"

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
                DEBUG_FATAL("No version specified/Incorrect version format for project: "+path.full);
                return nullptr;
            }
            else if(projectMajorVersion != VERSION_MAJOR || projectMinorVersion != VERSION_MINOR || projectPatchVersion != VERSION_PATCH){
                DEBUG_FATAL("Version incompatibility found between engine and project : "+path.full+ "\nProject version : "
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
                for(auto level : data["buildSettings"]){
                    if(!level.is_string())
                        continue;

                    buildSettings.AddToBuildSettings(Filesystem::Path(level));
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

            Projects::EditorPreferences editorPrefs = {};

            project = std::make_shared<Projects::Project>(path.GetFilename(false), projectRoot, projectResPath, pluginsPath, buildSettings, editorPrefs, assetDatabasePath);

            *project->GetPhysicsSettings() = physicsSettings;

            project->versionMajor = projectMajorVersion;
            project->versionMinor = projectMinorVersion;
            project->versionPatch = projectPatchVersion;

            return project;

        } catch (const json::parse_error& e) {
            DEBUG_ERROR("JSON parse error: " + (std::string)e.what());
            return nullptr;
        }
    }

    void SerializeProject(Projects::Project* pro, const Filesystem::Path path)
    {
        ordered_json data;

        data["versionMajor"] = pro->versionMajor;
        data["versionMinor"] = pro->versionMinor;
        data["versionPatch"] = pro->versionPatch;

        data["projectResources"] = pro->GetProjectResourcesPath().RelativeTo(pro->GetProjectRoot()).full;
        data["pluginsFolder"] = pro->GetPluginsFolderPath().RelativeTo(pro->GetProjectRoot()).full;
        data["assetDatabase"] = pro->GetAssetDatabasePath().RelativeTo(pro->GetProjectRoot()).full;

        for(auto& lvl : pro->GetBuildSettings()->buildIndex){
            data["buildSettings"].push_back(lvl.full);
        }

        Projects::PhysicsSettings* physics = pro->GetPhysicsSettings();

        data["physics"]["gravity"]["x"] = physics->gravity.x;
        data["physics"]["gravity"]["y"] = physics->gravity.y;
        data["physics"]["gravity"]["z"] = physics->gravity.z;
        data["physics"]["fixedTimeStep"] = physics->fixedTimeStep;
        data["physics"]["maxAccumulatedTime"] = physics->maxAccumulatedTime;

        //TODO data["editorPreferences"] = pro->GetEditorPrefs();

        path.WriteFile(data.dump());
    }
}