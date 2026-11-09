#include "engine/plugins/module_assets.hpp"

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/world/engine.hpp"

namespace Shard::Engine::Plugins {

    size_t RegisterModuleAssets(const ContentMounts& content, const std::string& pluginId, Filesystem::AssetIDManager& ids)
    {
        const ContentMounts::MountPoint* mount = content.Find(pluginId);
        if (!mount) return 0;
        // A second mount of the same module replaces the first
        ids.UnregisterModuleAssets(ContentMounts::MakeModUri(pluginId, ""));

        size_t added = 0;
        for (const std::string& file : content.List(pluginId)) {
            const std::string uri = ContentMounts::MakeModUri(pluginId, file);
            auto infos = std::make_shared<Filesystem::AssetInfos>();
            Filesystem::FileInfos& base = infos->baseInfos;
            // A folder gives the real file, a pak only the uri (the bytes are read through the mount)
            base.path = Filesystem::Path(mount->pak ? uri : mount->root + "/" + file);
            base.name = Filesystem::Path(file).GetFilename(false);
            base.extension = Filesystem::Path(file).GetExtensionString();
            base.type = Filesystem::Path(file).GetExtensionType();
            base.isDirectory = false;
            const Pak::Entry* entry = mount->pak ? mount->pak->Find(file) : nullptr;
            base.size = entry ? int(entry->size) : 0;
            if (ids.RegisterModuleAsset(uri, std::move(infos)).GetAsInt() >= 0) ++added;
        }
        return added;
    }

    size_t UnregisterModuleAssets(const std::string& pluginId, Filesystem::AssetIDManager& ids)
    {
        return ids.UnregisterModuleAssets(ContentMounts::MakeModUri(pluginId, ""));
    }

    size_t RegisterModuleAssets(const ContentMounts& content, const std::string& pluginId)
    {
        Core::IEngineContext* engine = Core::gSharedEnginePtr;
        Filesystem::AssetIDManager* ids = engine ? engine->GetAssetIDManager() : nullptr;
        return ids ? RegisterModuleAssets(content, pluginId, *ids) : 0;
    }

    size_t UnregisterModuleAssets(const std::string& pluginId)
    {
        Core::IEngineContext* engine = Core::gSharedEnginePtr;
        Filesystem::AssetIDManager* ids = engine ? engine->GetAssetIDManager() : nullptr;
        return ids ? UnregisterModuleAssets(pluginId, *ids) : 0;
    }
}
