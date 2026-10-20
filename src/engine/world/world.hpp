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
#include "engine/world/world_extension.hpp"
#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Objects{

    class Actor;
}

namespace Shard::Engine::Worlds{

    /// The resources a world file references (what loading the world needs), read straight from the file
    /// without building the world. Empty if the file can't be parsed.
    std::vector<Core::Resources::ResourceKey> CollectWorldAssetRefs(const Filesystem::Path& filePath);

    class World;

    /// @brief One actor and its subtree, in the form a world file stores it (name, components, children)
    ordered_json SerializeActorToJson(const std::shared_ptr<Objects::Actor>& actor);

    /// @brief Builds the actor that SerializeActorToJson described (its subtree too) in `world` : under `parent`, or at the root
    /// of the world when parent is null. The objects get new ids.
    std::shared_ptr<Objects::Actor> InstantiateActor(World& world, const json& actorData, const std::shared_ptr<Objects::Actor>& parent = nullptr);

    class World : public Core::TagContainer::Listener{

        std::unordered_map<Core::ObjectID, std::shared_ptr<Objects::Actor>> rootActors;
        std::string name;
        
        Filesystem::Path path;

        bool loaded = false;

        // Set on any editor edit to this world since it was last loaded/saved (see the various
        // panels/callers that flip this via SetDirty), cleared by Serialize()/Deserialize(). Drives
        // the "unsaved changes" warning popup shown before the world is replaced/unloaded.
        bool dirty = false;

        Filesystem::AssetID assetID;

        std::vector<std::string> folders;

        // What ResolveAssetID found last, and for which file (it is asked on every actor created or destroyed)
        int resolvedAssetID = -1;
        std::string resolvedAssetPath;

        public:
            World(std::string name, Filesystem::Path path);

            void Deserialize(Filesystem::Path filePath);
            void Serialize(Filesystem::Path filePath);

            void RemoveActorRecursive(Core::ObjectID actorID);

            void Clear();

            void Tick();
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

            /// @brief The outliner folders the editor made ("Lights", "Lights/Indoor"...), the empty ones included : the ones that hold
            /// actors are known from the actors (Actor::GetFolder). Saved with the world.
            std::vector<std::string>& GetFolders() { return folders; }
            const std::vector<std::string>& GetFolders() const { return folders; }
            
            void RemoveComponent(const int idInWorld, const std::shared_ptr<Objects::Components::Component> compPtr);

            void SetAssetID(Filesystem::AssetID assetID) {
                this->assetID = assetID;
            }

            Filesystem::AssetID GetAssetID() {
                return this->assetID;
            }

            /// @brief The ID of the world asset, found from the world's file : what the world structure events carry. Not taken from
            /// the build list, which Project Settings reorders while the world is open. -1 when the file is not in the asset database.
            int ResolveAssetID();

            /// @brief The nested reference frames of this world (floating origins : planets, ships, streaming cells...).
            /// The root frame is always there ; an actor says which frame it lives in with Transform::frame.
            Core::ReferenceFrameTree& GetFrames() { return frames; }
            const Core::ReferenceFrameTree& GetFrames() const { return frames; }

            bool IsLoaded() { return loaded; }

            void SetLoaded(bool loaded) { this->loaded = loaded; }

            bool IsDirty() { return dirty; }

            void SetDirty(bool dirty) { this->dirty = dirty; }

            /// The flat ambient light every surface gets (scaled by its albedo), added on top of the lighting. HDR : a component can exceed 1.
            glm::vec3 ambientColor = glm::vec3(0.3f);

            // Screen-space ambient occlusion (see SSAOManager). Off by default since it darkens every
            // scene's ambient term - the 3 SSAO render passes themselves always run regardless (turning
            // this on/off doesn't register/unregister them), only the multiply in lit.frag is gated by
            // it, so toggling this is cheap and instant. radius/bias/intensity are the classic
            // hemisphere-kernel SSAO tuning knobs (world-space sample radius, depth-compare bias to
            // avoid self-occlusion acne, and a 0-1 blend strength applied in lit.frag).
            bool ssaoEnabled = true;
            float ssaoRadius = 0.5f;
            float ssaoBias = 0.025f;
            float ssaoIntensity = 1.0f;
            // Contrast curve applied to the raw AO average (see ssao.frag) before it reaches
            // lit.frag - without it, a 32-sample boolean average clusters close to mid-gray and
            // never gets convincingly dark even in tight corners. 1.0 = no curve (legacy behavior).
            float ssaoPower = 2.0f;

            // These are maps for fast lookup (key: id IN WORLD, value: ptr to the comp). The components that
            // belong to the other modules (lights, models, physics bodies...) are kept by their module, in an
            // extension of the world.
            std::unordered_map<int, std::shared_ptr<Objects::Components::Transform>> transforms;
            // Ordered by id (= the order the components were added) : the scripts of a world tick in that order, on every
            // platform. The iteration order of a hash table is the library's business, and a replay cannot depend on it.
            std::map<int, std::shared_ptr<Objects::Components::Script>> scripts;

            /// @brief What the module that owns `T` keeps on this world (created on first use)
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

            /// @brief Called by the actors when a component joins the world, so the modules can index it
            void NotifyComponentAdded(int idInWorld, const std::shared_ptr<Objects::Components::Component>& component, bool cloned = false);

            // ---- Tags (see docs/concepts/tags.md) ----

            /// @brief The tags of the world itself. Saved with it ; changing them sends TagsChangedEvent to the world (its asset id).
            Core::TagContainer& GetTags() { return tags; }
            const Core::TagContainer& GetTags() const { return tags; }

            void OnTagsChanged(const Core::TagContainer& added, const Core::TagContainer& removed, bool quiet) override;

            /// @brief The actors that have `tag` (or a tag under it), in the order of their ids. The deactivated ones are left out
            /// unless includeInactive. Read from the world's index : costs the number of actors found, not of actors in the world.
            std::vector<std::shared_ptr<Objects::Actor>> FindActorsWithTag(Core::Tag tag, bool includeInactive = false);
            /// The first of FindActorsWithTag, null when there is none
            std::shared_ptr<Objects::Actor> FindFirstActorWithTag(Core::Tag tag, bool includeInactive = false);
            /// @brief The actors whose tags match `query`, in the order of their ids. Starts from the index when the query has a
            /// required tag (TagQuery::GetRequiredTag), and tests every actor of the world otherwise.
            std::vector<std::shared_ptr<Objects::Actor>> FindActors(const Core::TagQuery& query, bool includeInactive = false);

            /// The components that have `tag` (or a tag under it), in the order of their ids in the world
            std::vector<std::shared_ptr<Objects::Components::Component>> FindComponentsWithTag(Core::Tag tag, bool includeInactive = false);
            /// The same, only those of type T
            template<class T>
            std::vector<std::shared_ptr<T>> FindComponentsWithTag(Core::Tag tag, bool includeInactive = false)
            {
                std::vector<std::shared_ptr<T>> out;
                for (auto& component : FindComponentsWithTag(tag, includeInactive))
                    if (auto typed = std::dynamic_pointer_cast<T>(component))
                        out.push_back(std::move(typed));
                return out;
            }

            /// @brief The actors that match `query` and whose position (Transform::GetWorldCoords) is within `radius` of `center` :
            /// "the enemies within 10 m". In the order of their ids.
            void QuerySphere(const Core::DVec3& center, double radius, const Core::TagQuery& query,
                             std::vector<std::shared_ptr<Objects::Actor>>& out, bool includeInactive = false);
            /// The same, with the position inside `box`
            void QueryAABB(const Core::AABB& box, const Core::TagQuery& query,
                           std::vector<std::shared_ptr<Objects::Actor>>& out, bool includeInactive = false);

            /// @brief Keeps the tag index up to date : called by the actors when they join or leave the world and when their tags change
            void IndexActorTags(const std::shared_ptr<Objects::Actor>& actor, const Core::TagContainer& added, const Core::TagContainer& removed);
            /// The same for the components (removed from the index by RemoveComponent)
            void IndexComponentTags(int idInWorld, const std::shared_ptr<Objects::Components::Component>& component,
                                    const Core::TagContainer& added, const Core::TagContainer& removed);

        private:

            Core::ReferenceFrameTree frames;
            std::vector<std::pair<std::type_index, std::unique_ptr<IWorldExtension>>> extensions;

            Core::TagContainer tags;

            // The tag index : explicit tag -> what has it, ordered by id so that a search finds them in the same order on every run
            std::unordered_map<uint64_t, std::map<Core::ObjectID, std::weak_ptr<Objects::Actor>>> actorsByTag;
            std::unordered_map<uint64_t, std::map<int, std::weak_ptr<Objects::Components::Component>>> componentsByTag;

            /// The actors of the index under `tag` (its tree included), ordered by id
            std::map<Core::ObjectID, std::shared_ptr<Objects::Actor>> CollectTagged(Core::Tag tag, bool includeInactive);
            /// Every actor of the world, ordered by id
            std::map<Core::ObjectID, std::shared_ptr<Objects::Actor>> CollectAll(bool includeInactive);
    };

}
