#include "project.hpp"

#include "engine/assets/serialization/project/project_serializer.hpp"
#include "engine/assets/asset_database_serializer.hpp"
#include "engine/assets/asset_references.hpp"
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

    void Project::RemapWorlds(const std::string& oldName, const std::string& newName, bool isDirectory, const std::string& path)
    {
        Assets::RemapBuildSettings(buildSettings, projectResourcesRoot, oldName, newName, isDirectory);

        // The saved list, apart from the one being edited : the file gets the rename, not the changes that were not saved
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
    }
}
