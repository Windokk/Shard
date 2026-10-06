#include "resources_manager.hpp"

#include <algorithm>

#include "engine/assets/asset_database_serializer.hpp"
#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Core::Resources{

    using namespace Filesystem;

    ResourcesManager::ResourcesManager(Filesystem::FileManager& fileManager, Filesystem::AssetIDManager& assetIDManager)
        : files(&fileManager), ids(&assetIDManager)
    {
    }

    void ResourcesManager::RegisterKind(AssetKind kind, AssetKindInfo info)
    {
        const size_t index = static_cast<size_t>(kind);
        if(kinds.size() <= index){
            kinds.resize(index + 1);
            resident.resize(index + 1);
        }
        kinds[index] = std::move(info);
    }

    const AssetKindInfo* ResourcesManager::KindInfo(AssetKind kind) const
    {
        const size_t index = static_cast<size_t>(kind);
        if(index >= kinds.size() || !kinds[index])
            return nullptr;
        return &*kinds[index];
    }

    void ResourcesManager::ConstructGlobalFileIndex(const Filesystem::Path &projectResDir, const Filesystem::Path &projectDatabasePath)
    {
        Filesystem::Path engineDataBasePath = files->GetEngineResRoot() / "asset_database.json";

        Serialization::DeserializeAssetDataBase(files->GetEngineResRoot(), engineDataBasePath, *files, *ids);

        Serialization::DeserializeAssetDataBase(projectResDir, projectDatabasePath, *files, *ids);
    }

    // ---------------------------------------------------------------------------------------------
    // Cache
    // ---------------------------------------------------------------------------------------------

    std::shared_ptr<void> ResourcesManager::GetRaw(AssetKind kind, const std::string &pathInProject)
    {
        const size_t index = static_cast<size_t>(kind);

        if(index < resident.size()){
            auto it = resident[index].find(pathInProject);
            if(it != resident[index].end())
                return it->second;
        }

        const AssetKindInfo* info = KindInfo(kind);
        if(!info || !info->load){
            DEBUG_ERROR("No loader registered for asset kind " + std::to_string(index) + " (" + pathInProject + ")");
            return nullptr;
        }

        const std::string databaseName = pathInProject + info->nameSuffix;
        const Filesystem::AssetID id = ids->GetIDFromNameInProject(databaseName);
        std::shared_ptr<Filesystem::AssetInfos> assetInfos = ids->GetAssetFromID(id);

        if(assetInfos == nullptr){
            if(!info->unknownMessage.empty())
                DEBUG_ERROR(info->unknownMessage + pathInProject);
            return nullptr;
        }

        std::shared_ptr<void> resource = info->load(pathInProject, *assetInfos);
        if(!resource)
            return nullptr;

        resident[index].emplace(pathInProject, resource);
        if(info->setAssetID)
            info->setAssetID(resource.get(), id);

        // A material only stores its textures' raw GL handles, so it is the graph edge (not the
        // material's own pointers) that keeps them alive. Same for a world and its meshes, etc.
        if(info->dependencies)
            SetDependencies({kind, pathInProject}, info->dependencies(assetInfos->baseInfos.path));

        return resource;
    }

    void ResourcesManager::AdoptRaw(AssetKind kind, const std::string &pathInProject, std::shared_ptr<void> resource)
    {
        const AssetKindInfo* info = KindInfo(kind);
        if(!info){
            DEBUG_ERROR("No kind registered for asset kind " + std::to_string(static_cast<size_t>(kind)) + " (" + pathInProject + ")");
            return;
        }

        auto [it, inserted] = resident[static_cast<size_t>(kind)].emplace(pathInProject, std::move(resource));
        if(inserted && info->setAssetID)
            info->setAssetID(it->second.get(), ids->GetIDFromNameInProject(pathInProject + info->nameSuffix));
    }

    bool ResourcesManager::Has(AssetKind kind, const std::string &pathInProject) const
    {
        const size_t index = static_cast<size_t>(kind);
        return index < resident.size() && resident[index].find(pathInProject) != resident[index].end();
    }

    void ResourcesManager::Unload(AssetKind kind, const std::string &pathInProject)
    {
        const size_t index = static_cast<size_t>(kind);
        if(index < resident.size())
            resident[index].erase(pathInProject);

        const AssetKindInfo* info = KindInfo(kind);
        if(info && info->dependencies)
            ReleaseDependencies({kind, pathInProject});
    }

    // ---------------------------------------------------------------------------------------------
    // Dependency graph / residency
    // ---------------------------------------------------------------------------------------------

    bool ResourcesManager::ResolveAsset(const std::string& pathInProject, Filesystem::AssetID& outID) const
    {
        Filesystem::AssetID id = ids->GetIDFromNameInProject(pathInProject);
        auto info = ids->GetAssetFromID(id);
        if(!info || info->baseInfos.nameInProject != pathInProject)
            return false;
        outID = id;
        return true;
    }

    std::vector<PrefetchTask> ResourcesManager::PlanPrefetch(const std::string& ownerPathInProject)
    {
        std::vector<PrefetchTask> tasks;
        std::unordered_set<ResourceKey, ResourceKeyHash> seen;

        // Owners (world, materials) are walked through their dependency list, everything else is a leaf
        std::function<void(const std::string&, const Filesystem::Path&, const AssetKindInfo&)> visitOwner =
            [&](const std::string&, const Filesystem::Path& file, const AssetKindInfo& ownerInfo)
        {
            for(const ResourceKey& key : ownerInfo.dependencies(file)){
                if(!seen.insert(key).second)
                    continue;

                const AssetKindInfo* info = KindInfo(key.kind);
                if(!info)
                    continue;

                Filesystem::AssetID id;
                if(!ResolveAsset(key.path, id))
                    continue;
                Filesystem::Path keyFile = ids->GetAssetFromID(id)->baseInfos.path;

                if(info->dependencies)
                    visitOwner(key.path, keyFile, *info);

                if(!info->prefetch || Has(key.kind, key.path))
                    continue;

                PrefetchTask task;
                task.key = key;
                task.decode = [this, fn = info->prefetch, key, keyFile](){ return fn(*this, key.path, keyFile); };
                tasks.push_back(std::move(task));
            }
        };

        Filesystem::AssetID ownerID;
        if(!ResolveAsset(ownerPathInProject, ownerID))
            return tasks;

        auto ownerAsset = ids->GetAssetFromID(ownerID);
        for(size_t i = 0; i < kinds.size(); i++){
            if(kinds[i] && kinds[i]->dependencies && kinds[i]->ownerType && *kinds[i]->ownerType == ownerAsset->baseInfos.type){
                visitOwner(ownerPathInProject, ownerAsset->baseInfos.path, *kinds[i]);
                break;
            }
        }

        return tasks;
    }

    long ResourcesManager::UseCount(const ResourceKey& key) const
    {
        const size_t index = static_cast<size_t>(key.kind);
        if(index >= resident.size())
            return 0;
        auto it = resident[index].find(key.path);
        return it == resident[index].end() ? 0 : it->second.use_count();
    }

    void ResourcesManager::SetDependencies(const ResourceKey &owner, const std::vector<ResourceKey> &deps)
    {
        ReleaseDependencies(owner);

        std::vector<ResourceKey> unique;
        std::unordered_set<ResourceKey, ResourceKeyHash> seen;
        for(auto& dep : deps){
            if(seen.insert(dep).second){
                unique.push_back(dep);
                retainCount[dep]++;
            }
        }
        dependencies[owner] = unique;

        StoreDependencies(owner, unique);
    }

    void ResourcesManager::StoreDependencies(const ResourceKey &owner, const std::vector<ResourceKey> &unique)
    {
        // Mirror into the asset database entry (persisted with the project). A shader is two/three files
        // there, hence the extra IDs.
        Filesystem::AssetID ownerID;
        if(!ResolveAsset(owner.path, ownerID))
            return;

        std::vector<Filesystem::AssetID> assetIDs;
        auto addID = [&](const std::string& name){
            Filesystem::AssetID id;
            if(ResolveAsset(name, id) && std::find(assetIDs.begin(), assetIDs.end(), id) == assetIDs.end())
                assetIDs.push_back(id);
        };
        for(auto& dep : unique){
            const AssetKindInfo* info = KindInfo(dep.kind);
            if(info){
                for(const std::string& suffix : info->dependencySuffixes)
                    addID(dep.path + suffix);
            }
            else
                addID(dep.path);
        }
        ids->GetAssetFromID(ownerID)->dependencies = assetIDs;
    }

    void ResourcesManager::ReleaseDependencies(const ResourceKey &owner)
    {
        auto it = dependencies.find(owner);
        if(it == dependencies.end())
            return;

        for(auto& dep : it->second){
            auto count = retainCount.find(dep);
            if(count != retainCount.end() && --count->second <= 0)
                retainCount.erase(count);
            evictionCandidates.insert(dep);
        }
        dependencies.erase(it);
    }

    bool ResourcesManager::IsResident(const ResourceKey &key) const
    {
        return Has(key.kind, key.path);
    }

    bool ResourcesManager::IsEvictable(const ResourceKey &key) const
    {
        // Worlds are owned by the WorldManager (Unload), never swept.
        const AssetKindInfo* info = KindInfo(key.kind);
        if(!info || !info->evictable || retainCount.count(key) > 0)
            return false;

        // Not resident (0) or still held by something besides the cache (Model, skybox, pipeline...)
        if(UseCount(key) != 1)
            return false;

        // Engine resources (IDs <= 500, see SerializeAssetDataBase) back the renderer itself and are
        // used by materials without being listed anywhere : always resident.
        const std::string assetName = key.path + info->nameSuffix;

        Filesystem::AssetID id = ids->GetIDFromNameInProject(assetName);
        auto assetInfos = ids->GetAssetFromID(id);
        return assetInfos && assetInfos->baseInfos.nameInProject == assetName && id.GetAsInt() > 500;
    }

    void ResourcesManager::Evict(const ResourceKey &key)
    {
        // Unload() releases what this resource retained, which queues its dependencies for eviction
        Unload(key.kind, key.path);
    }

    int ResourcesManager::CollectUnused()
    {
        int evicted = 0;
        std::vector<ResourceKey> deferred;

        while(!evictionCandidates.empty()){
            ResourceKey key = *evictionCandidates.begin();
            evictionCandidates.erase(evictionCandidates.begin());

            if(!IsResident(key))
                continue;

            if(IsEvictable(key)){
                Evict(key);
                evicted++;
            }
            else{
                const AssetKindInfo* info = KindInfo(key.kind);
                if(retainCount.count(key) == 0 && info && info->evictable)
                    deferred.push_back(key); // nobody needs it but something still holds it : try again next time
            }
        }

        evictionCandidates.insert(deferred.begin(), deferred.end());
        return evicted;
    }

    void ResourcesManager::RefreshDependencies(const std::string &pathInProject)
    {
        Filesystem::AssetID id;
        if(!ResolveAsset(pathInProject, id))
            return;

        auto info = ids->GetAssetFromID(id);

        // The owner kind (world, material) is the one whose files have this asset type
        std::optional<AssetKind> ownerKind;
        for(size_t i = 0; i < kinds.size(); i++){
            if(kinds[i] && kinds[i]->dependencies && kinds[i]->ownerType && *kinds[i]->ownerType == info->baseInfos.type){
                ownerKind = static_cast<AssetKind>(i);
                break;
            }
        }
        if(!ownerKind)
            return;

        ResourceKey owner{*ownerKind, pathInProject};
        std::vector<ResourceKey> keys = kinds[static_cast<size_t>(*ownerKind)]->dependencies(info->baseInfos.path);

        // Retains only make sense for a resident owner : nothing would ever release them otherwise
        if(IsResident(owner))
            SetDependencies(owner, keys);
        else{
            std::vector<ResourceKey> unique;
            for(auto& k : keys)
                if(std::find(unique.begin(), unique.end(), k) == unique.end())
                    unique.push_back(k);
            StoreDependencies(owner, unique);
        }
    }

    bool ResourcesManager::IsInUse(const std::string &pathInProject) const
    {
        const std::string withoutExtension = pathInProject.substr(0, pathInProject.rfind('.'));

        for(size_t i = 0; i < kinds.size(); i++){
            if(!kinds[i])
                continue;

            // Shaders are keyed without their .vert/.frag/.comp extension
            const std::string& path = kinds[i]->nameSuffix.empty() ? pathInProject : withoutExtension;
            const ResourceKey key{static_cast<AssetKind>(i), path};

            if(retainCount.count(key) > 0 || UseCount(key) > 1)
                return true;
        }
        return false;
    }

    bool ResourcesManager::TryUnloadAsset(const std::string &pathInProject)
    {
        if(IsInUse(pathInProject))
            return false;

        const std::string withoutExtension = pathInProject.substr(0, pathInProject.rfind('.'));

        for(size_t i = 0; i < kinds.size(); i++){
            if(!kinds[i])
                continue;
            Unload(static_cast<AssetKind>(i), kinds[i]->nameSuffix.empty() ? pathInProject : withoutExtension);
        }

        CollectUnused();
        return true;
    }

    void ResourcesManager::UnLoadDependencies(const std::string &assetName)
    {
        Filesystem::AssetID id;
        if(!ResolveAsset(assetName, id))
            return;

        const Filesystem::Type type = ids->GetAssetFromID(id)->baseInfos.type;

        for(size_t i = 0; i < kinds.size(); i++){
            if(!kinds[i] || !kinds[i]->dependencies || !kinds[i]->ownerType || *kinds[i]->ownerType != type)
                continue;

            const ResourceKey key{static_cast<AssetKind>(i), assetName};

            if(kinds[i]->evictable){
                if(!IsEvictable(key))
                    return;
                Evict(key);
            }
            else
                Unload(key.kind, key.path); // owned elsewhere (worlds) : dropped on request

            CollectUnused();
            return;
        }
    }
}
