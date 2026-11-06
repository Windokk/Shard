#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Core::Resources{

    // The cache does not know the kinds. The owning module registers each one. Plugins and later modules use ids from FirstUser.
    enum class AssetKind : uint16_t {
        Mesh,
        Texture,
        EnvMap,
        Shader,
        ComputeShader,
        Material,
        World,
        Sound,
        ProbeBake,
        Lightmap,
        // Image read as sRGB colour (albedo, emission). Same files as Texture, but the GPU linearizes on sample.
        ColorTexture,
        FirstUser
    };

    struct ResourceKey {
        AssetKind kind;
        std::string path;
        bool operator==(const ResourceKey& o) const { return kind == o.kind && path == o.path; }
    };

    struct ResourceKeyHash {
        size_t operator()(const ResourceKey& k) const { return std::hash<std::string>()(k.path) ^ ((size_t)k.kind << 1); }
    };

    class ResourcesManager;

    struct PrefetchTask {
        ResourceKey key;
        // Runs on a worker thread. Returns the main-thread step (GPU upload, cache insert), or an empty function.
        std::function<std::function<void()>()> decode;
    };

    struct AssetKindInfo {
        std::function<std::shared_ptr<void>(const std::string& pathInProject, const Filesystem::AssetInfos& infos)> load;

        std::function<void(void* resource, Filesystem::AssetID id)> setAssetID;

        std::function<std::function<void()>(ResourcesManager& resources, const std::string& pathInProject, const Filesystem::Path& file)> prefetch;

        // Rebuilds in place so the object, AssetID and pointers stay valid. Main thread only. On false the old content must be untouched.
        std::function<bool(void* existing, const std::string& pathInProject, const Filesystem::AssetInfos& infos)> reload;

        std::function<std::vector<ResourceKey>(const Filesystem::Path& path)> dependencies;

        std::optional<Filesystem::Type> ownerType;

        std::string nameSuffix;

        std::vector<std::string> dependencySuffixes = {""};

        bool evictable = true;

        std::string unknownMessage;
    };

    struct AssetTypeExtension {
        std::string name;
        std::vector<std::string> extensions;
        AssetKindInfo info;
        AssetKind kind = AssetKind::FirstUser;
    };

    class ResourcesManager{
        public:

            ResourcesManager(Filesystem::FileManager& fileManager, Filesystem::AssetIDManager& assetIDManager);

            void RegisterKind(AssetKind kind, AssetKindInfo info);

            AssetKind AllocateUserKind() const;

            bool UnregisterKind(AssetKind kind);

            std::vector<PrefetchTask> PlanPrefetch(const std::string& ownerPathInProject);

            void ConstructGlobalFileIndex(const Filesystem::Path &projectResDir, const Filesystem::Path &projectDatabasePath);

            template<class T>
            std::shared_ptr<T> Get(AssetKind kind, const std::string& pathInProject)
            {
                return std::static_pointer_cast<T>(GetRaw(kind, pathInProject));
            }

            template<class T>
            void Adopt(AssetKind kind, const std::string& pathInProject, std::shared_ptr<T> resource)
            {
                AdoptRaw(kind, pathInProject, std::move(resource));
            }

            bool Reload(AssetKind kind, const std::string& pathInProject);

            bool Replace(AssetKind kind, const std::string& pathInProject);

            std::vector<ResourceKey> ResidentOwnersOf(const ResourceKey& key) const;

            std::vector<std::string> ResidentPaths(AssetKind kind) const;

            void RenameResident(const std::string& oldName, const std::string& newName, bool isDirectory);

            bool Has(AssetKind kind, const std::string& pathInProject) const;

            void Unload(AssetKind kind, const std::string& pathInProject);

            void UnLoadDependencies(const std::string &assetName);

            void RefreshDependencies(const std::string &pathInProject);

            int CollectUnused();

            bool IsInUse(const std::string &pathInProject) const;

            bool TryUnloadAsset(const std::string &pathInProject);

        private:

            std::shared_ptr<void> GetRaw(AssetKind kind, const std::string& pathInProject);
            void AdoptRaw(AssetKind kind, const std::string& pathInProject, std::shared_ptr<void> resource);

            const AssetKindInfo* KindInfo(AssetKind kind) const;
            // Unknown names come back as the default AssetID, which could coincide with a real one, so only trust an exact match.
            bool ResolveAsset(const std::string& pathInProject, Filesystem::AssetID& outID) const;
            long UseCount(const ResourceKey& key) const;

            void SetDependencies(const ResourceKey& owner, const std::vector<ResourceKey>& deps);
            void StoreDependencies(const ResourceKey& owner, const std::vector<ResourceKey>& deps);
            void ReleaseDependencies(const ResourceKey& owner);
            bool IsResident(const ResourceKey& key) const;
            bool IsEvictable(const ResourceKey& key) const;
            void Evict(const ResourceKey& key);

            Filesystem::FileManager* files;
            Filesystem::AssetIDManager* ids;

            std::vector<std::optional<AssetKindInfo>> kinds;
            std::vector<std::unordered_map<std::string, std::shared_ptr<void>>> resident;

            std::unordered_map<ResourceKey, int, ResourceKeyHash> retainCount;
            std::unordered_map<ResourceKey, std::vector<ResourceKey>, ResourceKeyHash> dependencies;
            std::unordered_set<ResourceKey, ResourceKeyHash> evictionCandidates;
        };

}
