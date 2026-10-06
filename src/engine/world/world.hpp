#pragma once

#include <string>
#include <vector>
#include <typeindex>

#include "engine/world/objectID.hpp"
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

    class World{

        std::unordered_map<Core::ObjectID, std::shared_ptr<Objects::Actor>> rootActors;
        std::string name;
        
        Filesystem::Path path;

        bool loaded = false;

        // Set on any editor edit to this world since it was last loaded/saved (see the various
        // panels/callers that flip this via SetDirty), cleared by Serialize()/Deserialize(). Drives
        // the "unsaved changes" warning popup shown before the world is replaced/unloaded.
        bool dirty = false;

        int buildIndex = -1;

        Filesystem::AssetID assetID;

        public:
            World(std::string name, Filesystem::Path path);

            void Deserialize(Filesystem::Path filePath);
            void Serialize(Filesystem::Path filePath);

            void SetBuildIndex(int buildIndex);

            void RemoveActorRecursive(Core::ObjectID actorID);

            void Clear();

            void Tick();
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
            
            void RemoveComponent(const int idInWorld, const std::shared_ptr<Objects::Components::Component> compPtr);

            void SetAssetID(Filesystem::AssetID assetID) {
                this->assetID = assetID;
            }

            Filesystem::AssetID GetAssetID() {
                return this->assetID;
            }

            int GetBuildIndex() { return buildIndex; }

            bool IsLoaded() { return loaded; }

            void SetLoaded(bool loaded) { this->loaded = loaded; }

            bool IsDirty() { return dirty; }

            void SetDirty(bool dirty) { this->dirty = dirty; }

            float ambientIntensity = 0.3f;

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
            std::unordered_map<int, std::shared_ptr<Objects::Components::Script>> scripts;

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

        private:

            std::vector<std::pair<std::type_index, std::unique_ptr<IWorldExtension>>> extensions;
    };

}
