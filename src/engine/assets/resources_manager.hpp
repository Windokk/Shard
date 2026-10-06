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

    /// The kinds of resources the cache holds. This layer does not know what a kind is : the module that
    /// owns the type registers it (renderer : Mesh..Material and ProbeBake, audio : Sound, world : World)
    /// with ResourcesManager::RegisterKind. Modules added later (genres, game) use ids from FirstUser.
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

    /// A resource to decode ahead of time (see AssetKindInfo::prefetch and ResourcesManager::PlanPrefetch).
    struct PrefetchTask {
        ResourceKey key;
        /// Runs on a worker thread : reads and decodes the file. Returns what the main thread has to run
        /// to finish the job (GPU upload, insertion in the cache), or an empty function if there is
        /// nothing to apply.
        std::function<std::function<void()>()> decode;
    };

    /// What the cache needs to know about a kind of resource, supplied by the module that owns it.
    struct AssetKindInfo {
        /// Builds the resource from its asset database entry (already resolved by the manager).
        /// Returns null if it can't be loaded.
        std::function<std::shared_ptr<void>(const std::string& pathInProject, const Filesystem::AssetInfos& infos)> load;

        /// Gives a resource that enters the cache (loaded or adopted) its AssetID. Optional.
        std::function<void(void* resource, Filesystem::AssetID id)> setAssetID;

        /// Optional : lets a world load decode this kind of resource on a worker thread. Called on that thread
        /// with the manager, the resource's path in the project and its file; returns the main-thread
        /// continuation (usually an `Adopt`), or an empty function if the file can't be used.
        std::function<std::function<void()>(ResourcesManager& resources, const std::string& pathInProject, const Filesystem::Path& file)> prefetch;

        /// Only for kinds that keep other resources alive (world, material) : the resources the file at
        /// `path` needs. Such a resource is an "owner" in the dependency graph.
        std::function<std::vector<ResourceKey>(const Filesystem::Path& path)> dependencies;

        /// Asset type of the files of an owner kind (worlds, materials), to find the kind of a file.
        std::optional<Filesystem::Type> ownerType;

        /// The resource is keyed by its path in the project, but its asset database entry may be named
        /// with a suffix on top of it (shaders : ".vert", compute shaders : ".comp").
        std::string nameSuffix;

        /// Database names a resource of this kind stands for when it is somebody's dependency
        /// (path + suffix). A shader is three files.
        std::vector<std::string> dependencySuffixes = {""};

        /// False if something else owns the lifetime of these resources (the world manager for worlds) :
        /// the cache then never sweeps them.
        bool evictable = true;

        /// Logged (followed by the path) when the asset database has no entry for a requested path.
        /// Empty : silent.
        std::string unknownMessage;
    };

    class ResourcesManager{
        public:

            ResourcesManager(Filesystem::FileManager& fileManager, Filesystem::AssetIDManager& assetIDManager);

            /// @brief Declares a kind of resource. Call it before anything of that kind is requested.
            void RegisterKind(AssetKind kind, AssetKindInfo info);

            /// @brief The resources the owner asset (a world) needs, directly or through other owners (its
            /// materials' textures...), that are not resident yet and whose kind can be decoded ahead of time.
            std::vector<PrefetchTask> PlanPrefetch(const std::string& ownerPathInProject);

            /// @brief Indexes all assets in the project and engine directories with unique IDs.
            /// @param projectResDir Path to the project's asset directory.
            /// @param projectDatabasePath Path to the project's asset database.
            void ConstructGlobalFileIndex(const Filesystem::Path &projectResDir, const Filesystem::Path &projectDatabasePath);

            /// @brief Retrieves a resource from the cache (Tries to load it if it isn't loaded yet)
            /// @param kind The kind of resource
            /// @param pathInProject The path of the resource in the project
            /// @return A shared pointer to the resource, null if it is unknown or can't be loaded
            template<class T>
            std::shared_ptr<T> Get(AssetKind kind, const std::string& pathInProject)
            {
                return std::static_pointer_cast<T>(GetRaw(kind, pathInProject));
            }

            /// @brief Inserts an already-built resource into the cache under `pathInProject`. No-op if `pathInProject` is already cached.
            template<class T>
            void Adopt(AssetKind kind, const std::string& pathInProject, std::shared_ptr<T> resource)
            {
                AdoptRaw(kind, pathInProject, std::move(resource));
            }

            /// @brief True if `pathInProject` is already resident in the cache.
            bool Has(AssetKind kind, const std::string& pathInProject) const;

            /// @brief Drops a resource from the cache. A kind with dependencies also releases what it kept alive.
            /// Other resources that were holding it keep it alive on their own.
            void Unload(AssetKind kind, const std::string& pathInProject);

            /// @brief Unloads a world/material and, recursively, every dependency nothing else needs. A no-op
            /// if the asset itself is still needed (a loaded world depends on it, or a Model holds it) - it
            /// would otherwise be left pointing at released resources (materials store raw texture handles).
            /// @param assetName The name of the "root" asset
            void UnLoadDependencies(const std::string &assetName);

            /// @brief Re-reads a world/material file and rebuilds its dependency list (in the resident graph
            /// and in the asset database entry, which is persisted with the project). Call it after such a
            /// file was saved. No-op for other asset types or unknown assets.
            void RefreshDependencies(const std::string &pathInProject);

            /// @brief Evicts every resource that was released by an unloaded world/asset (Unload, UnLoadDependencies) and that nothing needs anymore.
            /// @return The number of evicted resources
            int CollectUnused();

            /// @brief True if something still needs the asset : a loaded world/material depends on it, or
            /// something outside the cache holds it (a Model, the skybox, the loaded world itself...).
            /// Such an asset must not be renamed, moved or deleted from under its users.
            bool IsInUse(const std::string &pathInProject) const;

            /// @brief Drops the asset from every cache (and whatever only it kept alive) so its file can be
            /// renamed/moved/deleted. Returns false, changing nothing, if the asset is in use.
            bool TryUnloadAsset(const std::string &pathInProject);

        private:

            std::shared_ptr<void> GetRaw(AssetKind kind, const std::string& pathInProject);
            void AdoptRaw(AssetKind kind, const std::string& pathInProject, std::shared_ptr<void> resource);

            const AssetKindInfo* KindInfo(AssetKind kind) const;
            /// Resolves a path in the project to its asset ID, only if it really is that asset (an unknown
            /// name comes back as the default AssetID, which could coincide with a real one).
            bool ResolveAsset(const std::string& pathInProject, Filesystem::AssetID& outID) const;
            long UseCount(const ResourceKey& key) const;

            /// Replaces the set of resources the resident `owner` keeps alive (each one gets +1 retain), and mirrors it into the owner's AssetInfos::dependencies.
            void SetDependencies(const ResourceKey& owner, const std::vector<ResourceKey>& deps);
            /// Only the asset database half of SetDependencies : for an owner that is not resident (nothing to retain).
            void StoreDependencies(const ResourceKey& owner, const std::vector<ResourceKey>& deps);
            /// Drops the retains `owner` holds. The released keys become eviction candidates.
            void ReleaseDependencies(const ResourceKey& owner);
            bool IsResident(const ResourceKey& key) const;
            /// Resident, not retained, not held outside the cache, not an engine resource.
            bool IsEvictable(const ResourceKey& key) const;
            void Evict(const ResourceKey& key);

            Filesystem::FileManager* files;
            Filesystem::AssetIDManager* ids;

            /// Indexed by AssetKind
            std::vector<std::optional<AssetKindInfo>> kinds;
            std::vector<std::unordered_map<std::string, std::shared_ptr<void>>> resident;

            /// Retains held by resident owners on each resource (absent = 0)
            std::unordered_map<ResourceKey, int, ResourceKeyHash> retainCount;
            /// What each resident owner (world, material) retains
            std::unordered_map<ResourceKey, std::vector<ResourceKey>, ResourceKeyHash> dependencies;
            /// Released since the last CollectUnused()
            std::unordered_set<ResourceKey, ResourceKeyHash> evictionCandidates;
        };

}
