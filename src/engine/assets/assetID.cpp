#include "assetID.hpp"

#include <cctype>
#include <cstdint>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Filesystem{
    
    void AssetIDManager::DestroyID(const AssetID &id)
    {
        availableIDs.insert(id.GetAsInt());
        AssetIDMap.erase(id);
    }

    AssetID AssetIDManager::RegisterModuleAsset(const std::string& name, std::shared_ptr<AssetInfos> info)
    {
        uint32_t hash = 2166136261u;
        for (char c : name) {
            hash ^= static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(c)));
            hash *= 16777619u;
        }
        constexpr uint32_t kRange = 0x40000000u;
        for (uint32_t probe = 0; probe < 64; ++probe) {
            const AssetID id(kFirstModuleAssetID + int((hash + probe) % kRange));
            auto it = AssetIDMap.find(id);
            if (it != AssetIDMap.end() && it->second->baseInfos.nameInProject != name) continue;
            info->baseInfos.ID = id;
            info->baseInfos.nameInProject = name;
            AssetIDMap[id] = std::move(info);
            return id;
        }
        return AssetID(-1);
    }

    size_t AssetIDManager::UnregisterModuleAssets(const std::string& namePrefix)
    {
        size_t removed = 0;
        for (auto it = AssetIDMap.lower_bound(AssetID(kFirstModuleAssetID)); it != AssetIDMap.end();) {
            if (it->second->baseInfos.nameInProject.compare(0, namePrefix.size(), namePrefix) == 0) {
                it = AssetIDMap.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        return removed;
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

    AssetID AssetIDManager::GetIDFromNameInProject(const std::string& nameInProject)
    {
        auto cached = m_nameCache.find(nameInProject);
        if (cached != m_nameCache.end())
        {
            auto it = AssetIDMap.find(cached->second);
            if (it != AssetIDMap.end() && it->second->baseInfos.nameInProject == nameInProject)
                return cached->second;
            m_nameCache.erase(cached);
        }

        for (const auto& [id, info] : AssetIDMap)
        {
            if(info->baseInfos.nameInProject == nameInProject){
                m_nameCache[nameInProject] = id;
                return id;
            }
        }

        return AssetID{};
    }
}
