#include "project.hpp"

#include "engine/assets/serialization/project/project_serializer.hpp"
#include "engine/assets/asset_database_serializer.hpp"
#include "engine/assets/asset_references.hpp"
#include "engine/assets/serialization/collision/collision_json.hpp"
#include "engine/core/collision/collision_profile.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/core/tags/tag_registry.hpp"

namespace Shard::Engine::Projects{
    Project::Project(std::string name, Filesystem::Path projectRoot, Filesystem::Path projectResourcesRoot, Filesystem::Path pluginsFolder, BuildSettings buildSettings, Filesystem::Path assetDatabasePath)
    {
        this->name = name;
        this->projectRoot = projectRoot;
        this->projectResourcesRoot = projectResourcesRoot;
        this->pluginsFolder = pluginsFolder;
        this->buildSettings = buildSettings;
        this->assetDatabasePath = assetDatabasePath;
    }

    void Project::Shutdown(const Filesystem::AssetIDManager& assetIDManager)
    {
        Serialization::SerializeAssetDataBase(assetDatabasePath, assetIDManager);
    }

    bool Project::Save(const std::string& path)
    {
        const std::string text = Serialization::ProjectToJson(this).dump();
        if(!Filesystem::Path(path).WriteFile(text))
            return false;
        savedState = text;
        return true;
    }

    void Project::MarkSaved()
    {
        savedState = Serialization::ProjectToJson(this).dump();
    }

    bool Project::HasUnsavedChanges()
    {
        return Serialization::ProjectToJson(this).dump() != savedState;
    }

    static std::string RemappedAssetName(const std::string& name, const std::string& oldName, const std::string& newName, bool isDirectory)
    {
        if(name.empty() || newName.empty())
            return name;
        if(name == oldName)
            return newName;
        if(isDirectory && name.size() > oldName.size() && name.compare(0, oldName.size(), oldName) == 0 && name[oldName.size()] == '/')
            return newName + name.substr(oldName.size());
        return name;
    }

    void Project::RemapPhysicsMaterials(const std::string& oldName, const std::string& newName, bool isDirectory, const std::string& path)
    {
        bool changed = false;
        for(Collision::Profile& profile : collisionSettings.profiles){
            const std::string renamed = RemappedAssetName(profile.material, oldName, newName, isDirectory);
            if(renamed != profile.material){
                profile.material = renamed;
                changed = true;
            }
        }
        if(!changed)
            return;

        ApplyCollisionSettings(collisionSettings);

        nlohmann::ordered_json saved = nlohmann::ordered_json::parse(savedState, nullptr, false);
        if(saved.is_discarded() || !saved.is_object() || !saved.contains("collision_profiles") || !saved["collision_profiles"].is_object())
            return;

        bool savedChanged = false;
        for(auto& [name, profile] : saved["collision_profiles"].items()){
            if(!profile.is_object() || !profile.contains("material") || !profile["material"].is_string())
                continue;
            const std::string current = profile["material"].get<std::string>();
            const std::string renamed = RemappedAssetName(current, oldName, newName, isDirectory);
            if(renamed != current){
                profile["material"] = renamed;
                savedChanged = true;
            }
        }
        if(!savedChanged)
            return;

        const std::string text = saved.dump();
        if(!path.empty() && !Filesystem::Path(path).WriteFile(text)){
            DEBUG_ERROR("Could not write the physics material references that follow the rename to the project file : " + path);
            return;
        }
        savedState = text;
    }

    void Project::RemapWorlds(const std::string& oldName, const std::string& newName, bool isDirectory, const std::string& path)
    {
        RemapPhysicsMaterials(oldName, newName, isDirectory, path);
        Assets::RemapBuildSettings(buildSettings, projectResourcesRoot, oldName, newName, isDirectory);

        nlohmann::ordered_json saved = nlohmann::ordered_json::parse(savedState, nullptr, false);
        if(saved.is_discarded() || !saved.is_object())
            return;

        BuildSettings savedBuild;
        if(saved.contains("buildSettings") && saved["buildSettings"].is_array())
            for(const auto& world : saved["buildSettings"])
                if(world.is_string())
                    savedBuild.buildIndex.push_back(Filesystem::Path(world.get<std::string>()));

        Assets::RemapBuildSettings(savedBuild, projectResourcesRoot, oldName, newName, isDirectory);

        nlohmann::ordered_json list = nlohmann::ordered_json::array();
        for(const Filesystem::Path& world : savedBuild.buildIndex)
            list.push_back(world.full);
        if(saved["buildSettings"] == list)
            return;
        saved["buildSettings"] = list;

        const std::string text = saved.dump();
        if(!path.empty() && !Filesystem::Path(path).WriteFile(text)){
            DEBUG_ERROR("Could not write the build list that follows the rename to the project file : " + path);
            return;
        }
        savedState = text;
    }

    void ApplyTagSettings(const TagSettings& settings)
    {
        std::vector<std::pair<std::string, std::string>> tags(settings.tags.begin(), settings.tags.end());
        Core::TagRegistry& registry = Core::TagRegistry::Get();
        registry.SetDeclarations(Core::TagSource::Project, "", tags);
        registry.SetRedirects(settings.redirects);
        Collision::ProfileRegistry::Get().SetCollisionRoots(Collision::Source::Project, "", std::vector<std::string>(settings.collisionTags.begin(), settings.collisionTags.end()));
    }

    void ApplyCollisionSettings(const CollisionSettings& settings)
    {
        Collision::ProfileRegistry& registry = Collision::ProfileRegistry::Get();
        registry.SetDeclarations(Collision::Source::Project, "", settings.profiles);
    }

    void ReloadPhysicsMaterials(const Filesystem::AssetIDManager& ids)
    {
        std::vector<std::string> problems;
        std::vector<Collision::PhysicsMaterial> materials = Serialization::LoadPhysicsMaterialAssets(ids, &problems);
        for(const std::string& problem : problems)
            DEBUG_WARNING("[Physics] ", problem);
        Collision::ProfileRegistry::Get().SetMaterials(Collision::Source::Project, "", std::move(materials));
    }
}
