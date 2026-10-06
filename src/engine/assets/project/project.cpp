#include "project.hpp"

#include "engine/assets/serialization/project/project_serializer.hpp"
#include "engine/assets/asset_database_serializer.hpp"

namespace Shard::Engine::Projects{
    Project::Project(std::string name, Filesystem::Path projectRoot, Filesystem::Path projectResourcesRoot, Filesystem::Path pluginsFolder, BuildSettings buildSettings, EditorPreferences editorPreferences, Filesystem::Path assetDatabasePath)
    {
        this->name = name;
        this->projectRoot = projectRoot;
        this->projectResourcesRoot = projectResourcesRoot;
        this->pluginsFolder = pluginsFolder;
        this->buildSettings = buildSettings;
        this->editorPreferences = editorPreferences;
        this->assetDatabasePath = assetDatabasePath;
    }

    void Project::Shutdown(std::string path, const Filesystem::AssetIDManager& assetIDManager)
    {
        Serialization::SerializeProject(this, path);
        Serialization::SerializeAssetDataBase(assetDatabasePath, assetIDManager);
    }
}
