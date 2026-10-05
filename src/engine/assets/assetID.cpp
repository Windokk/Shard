#include "assetID.hpp"

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Filesystem{
    
    void AssetIDManager::DestroyID(const AssetID &id)
    {
        availableIDs.insert(id.GetAsInt());
        AssetIDMap.erase(id);
    }

    void AssetIDManager::AssignID(AssetID id, std::shared_ptr<AssetInfos> info)
    {
        AssetIDMap[id] = info;
    }

    std::shared_ptr<AssetInfos> AssetIDManager::GetAssetFromID(AssetID id)
    {
        auto it = AssetIDMap.find(id);
        if (it != AssetIDMap.end()) {
            return it->second;
        }
        return nullptr;
    }

    AssetID AssetIDManager::GetIDFromNameInProject(const std::string nameInProject)
    {
        for (const auto& [id, info] : AssetIDMap)
        {
            if(info->baseInfos.nameInProject == nameInProject){
                return id;
            }
        }

        return AssetID{};
    }
}
