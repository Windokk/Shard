#pragma once

#include <string>
#include <vector>

#include "engine/core/objectID.hpp"
#include "engine/objects/components/misc/script.hpp"
#include "engine/objects/components/rendering/model_component.hpp"
#include "engine/objects/skybox/skybox.hpp"
#include "engine/filesystem/filesystem.hpp"

namespace Shard::Engine::Objects{

    class Actor;

    namespace Components{
        class Light;
        class Camera;
        class Model;
        class Script;
        class AudioSource;
        class ProbeVolume;
    }
}

namespace Shard::Engine::Rendering{
    class Renderer;
    class Mesh;
    class Texture2D;
}

namespace Shard::Engine::Levels{

    struct LevelAssetManifest
    {
        bool success = false;
        std::vector<std::string> meshPathsInProject;
        std::vector<std::string> materialPathsInProject;
        std::vector<std::string> probeBakePathsInProject;
        std::string skyboxEnvMapPathInProject;
    };

    LevelAssetManifest CollectLevelAssetRefs(const Filesystem::Path& filePath);

    class Level{

        std::unordered_map<Core::ObjectID, std::shared_ptr<Objects::Actor>> rootActors;
        std::string name;
        
        Filesystem::Path path;

        bool loaded = false;

        // Set on any editor edit to this level since it was last loaded/saved (see the various
        // panels/callers that flip this via SetDirty), cleared by Serialize()/Deserialize(). Drives
        // the "unsaved changes" warning popup shown before the level is replaced/unloaded.
        bool dirty = false;

        int buildIndex = -1;

        Filesystem::AssetID assetID;

        public:
            Level(std::string name, Filesystem::Path path);

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
            
            void RemoveComponent(const int idInLevel, const std::shared_ptr<Objects::Components::Component> compPtr);

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

            /// @brief Makes `pathInProject` (an equirectangular RGB image, e.g. an .hdr) this level's skybox,
            /// creating the skybox if the level has none. Takes effect immediately - the sky, the IBL
            /// lighting and the probes' sky term all read level->skybox every frame - but note the map
            /// is loaded and IBL-convolved synchronously the first time it is used.
            /// @return false (logged) if the file can't be used as an environment map; the level's current
            /// skybox, if any, is then left as it was.
            bool SetSkybox(const std::string& pathInProject);

            /// @brief Removes the level's skybox (and its draw command). No-op if it has none.
            void ClearSkybox();

            /// @brief Path in the project of the skybox's image, or empty if the level has no skybox. This
            /// is what the level file stores.
            std::string GetSkyboxPath() const;

            float ambientIntensity = 0.3f;
            std::shared_ptr<Objects::Skybox> skybox;
            std::shared_ptr<Rendering::Texture2D> ibl_texture;

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

            std::shared_ptr<Objects::Components::ProbeVolume> probeVolume;

            // These are maps for fast lookup (key: id IN LEVEL, value: ptr to the comp)
            std::vector<std::shared_ptr<Objects::Components::Light>> lights;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Light>> lightComps;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Transform>> transforms;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Model>> models;
            std::unordered_map<int, std::shared_ptr<Objects::Components::PhysicsBody>> physicsBodies;
            std::unordered_map<int, std::shared_ptr<Objects::Components::AudioSource>> audioSources;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Camera>> cameras;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Script>> scripts;
            std::unordered_map<int, std::pair<glm::mat4, Rendering::Mesh*>> meshes;
            
    };

}
