#pragma once

#include <map>
#include <string>
#include <vector>
#include <typeindex>

#include "engine/world/objectID.hpp"
#include "engine/core/spatial/spatial_index.hpp"
#include "engine/core/tags/tag_container.hpp"
#include "engine/core/tags/tag_query.hpp"
#include "engine/world/components/script.hpp"
#include "engine/world/components/transform.hpp"
#include "engine/world/scheduler.hpp"
#include "engine/world/world_extension.hpp"
#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Objects{

    class Actor;
}

namespace Shard::Engine::Worlds{

    std::vector<Core::Resources::ResourceKey> CollectWorldAssetRefs(const Filesystem::Path& filePath);

    class World;

    ordered_json SerializeActorToJson(const std::shared_ptr<Objects::Actor>& actor);

    std::shared_ptr<Objects::Actor> InstantiateActor(World& world, const json& actorData, const std::shared_ptr<Objects::Actor>& parent = nullptr);

    class World : public Core::TagContainer::Listener{

        std::unordered_map<Core::ObjectID, std::shared_ptr<Objects::Actor>> rootActors;
        std::string name;
        
        Filesystem::Path path;

        bool loaded = false;

        bool dirty = false;

        Filesystem::AssetID assetID;

        std::vector<std::string> folders;

        int resolvedAssetID = -1;
        std::string resolvedAssetPath;
        bool resolvedAssetMissing = false;
        size_t resolvedAssetCount = 0;

        public:
            World(std::string name, Filesystem::Path path);

            void Deserialize(Filesystem::Path filePath);
            void Serialize(Filesystem::Path filePath);

            void RemoveActorRecursive(Core::ObjectID actorID);

            void Clear();

            void Tick();
            void EditTick();
            void FixedTick();
            void LateTick();
            void Play();
            void Stop();
            void OnLoad();
            void Unload();

            Filesystem::Path GetPath() { return path; }

            void AddActor(std::shared_ptr<Objects::Actor> a);
            void RemoveActor(Core::ObjectID id);
            std::shared_ptr<Objects::Actor> GetActor(Core::ObjectID id, bool recursive = false);
            std::vector<Core::ObjectID> GetActorsID(bool recursive = false);
            std::unordered_map<Core::ObjectID, std::shared_ptr<Objects::Actor>> GetRootActors() { return rootActors; };

            const std::string& GetName() const;
            void SetName(const std::string& name);

            std::vector<std::string>& GetFolders() { return folders; }
            const std::vector<std::string>& GetFolders() const { return folders; }
            
            void RemoveComponent(const int idInWorld, const std::shared_ptr<Objects::Components::Component> compPtr);

            void SetAssetID(Filesystem::AssetID assetID) {
                this->assetID = assetID;
            }

            Filesystem::AssetID GetAssetID() {
                return this->assetID;
            }

            // Found from the world's file, not the build list, which Project Settings reorders while the world is open. -1 if not in the asset database.
            int ResolveAssetID();

            Scheduler& GetScheduler() { return *scheduler; }
            // For what must outlive the world without keeping it : a script stopping its timers after its world is gone.
            std::weak_ptr<Scheduler> GetSchedulerRef() { return scheduler; }

            Core::ReferenceFrameTree& GetFrames() { return frames; }
            const Core::ReferenceFrameTree& GetFrames() const { return frames; }

            bool IsLoaded() { return loaded; }

            void SetLoaded(bool loaded) { this->loaded = loaded; }

            bool IsDirty() { return dirty; }

            void SetDirty(bool dirty) { this->dirty = dirty; }

            glm::vec3 ambientColor = glm::vec3(0.3f);

            bool ssaoEnabled = true;
            float ssaoRadius = 0.5f;
            float ssaoBias = 0.025f;
            float ssaoIntensity = 1.0f;
            float ssaoPower = 2.0f;

            std::unordered_map<int, std::shared_ptr<Objects::Components::Transform>> transforms;
            // Ordered by id so scripts tick in the same order on every platform. Hash table order is not replay safe.
            std::map<int, std::shared_ptr<Objects::Components::Script>> scripts;

            template<class T>
            T& Ext()
            {
                static_assert(std::is_base_of<IWorldExtension, T>::value, "T must derive from IWorldExtension");
                for(auto& [type, extension] : extensions){
                    if(type == std::type_index(typeid(T)))
                        return static_cast<T&>(*extension);
                }
                extensions.emplace_back(std::type_index(typeid(T)), std::make_unique<T>());
                return static_cast<T&>(*extensions.back().second);
            }

            void NotifyComponentAdded(int idInWorld, const std::shared_ptr<Objects::Components::Component>& component, bool cloned = false);

            Core::TagContainer& GetTags() { return tags; }
            const Core::TagContainer& GetTags() const { return tags; }

            void OnTagsChanged(const Core::TagContainer& added, const Core::TagContainer& removed, bool quiet) override;

            std::vector<std::shared_ptr<Objects::Actor>> FindActorsWithTag(Core::Tag tag, bool includeInactive = false);
            std::shared_ptr<Objects::Actor> FindFirstActorWithTag(Core::Tag tag, bool includeInactive = false);
            std::vector<std::shared_ptr<Objects::Actor>> FindActors(const Core::TagQuery& query, bool includeInactive = false);

            std::vector<std::shared_ptr<Objects::Components::Component>> FindComponentsWithTag(Core::Tag tag, bool includeInactive = false);
            template<class T>
            std::vector<std::shared_ptr<T>> FindComponentsWithTag(Core::Tag tag, bool includeInactive = false)
            {
                std::vector<std::shared_ptr<T>> out;
                for (auto& component : FindComponentsWithTag(tag, includeInactive))
                    if (auto typed = std::dynamic_pointer_cast<T>(component))
                        out.push_back(std::move(typed));
                return out;
            }

            void QuerySphere(const Core::DVec3& center, double radius, const Core::TagQuery& query,
                             std::vector<std::shared_ptr<Objects::Actor>>& out, bool includeInactive = false);
            void QueryAABB(const Core::AABB& box, const Core::TagQuery& query,
                           std::vector<std::shared_ptr<Objects::Actor>>& out, bool includeInactive = false);

            void IndexActorTags(const std::shared_ptr<Objects::Actor>& actor, const Core::TagContainer& added, const Core::TagContainer& removed);
            void IndexComponentTags(int idInWorld, const std::shared_ptr<Objects::Components::Component>& component,
                                    const Core::TagContainer& added, const Core::TagContainer& removed);

        private:

            Core::ReferenceFrameTree frames;
            std::shared_ptr<Scheduler> scheduler = std::make_shared<Scheduler>();
            std::vector<std::pair<std::type_index, std::unique_ptr<IWorldExtension>>> extensions;

            Core::TagContainer tags;

            std::unordered_map<uint64_t, std::map<Core::ObjectID, std::weak_ptr<Objects::Actor>>> actorsByTag;
            std::unordered_map<uint64_t, std::map<int, std::weak_ptr<Objects::Components::Component>>> componentsByTag;

            std::map<Core::ObjectID, std::shared_ptr<Objects::Actor>> CollectTagged(Core::Tag tag, bool includeInactive);
            std::map<Core::ObjectID, std::shared_ptr<Objects::Actor>> CollectAll(bool includeInactive);
    };

}
