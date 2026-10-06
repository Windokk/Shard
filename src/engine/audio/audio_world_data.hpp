#pragma once

#include <memory>
#include <unordered_map>

#include "engine/world/world_extension.hpp"

namespace Shard::Engine::Objects::Components{
    class AudioSource;
    class AudioListener;
}

namespace Shard::Engine::Audio{

    /// What the audio keeps on a world : its sources and listeners, by id in the world.
    class AudioWorldData : public Worlds::IWorldExtension{
        public:
            std::unordered_map<int, std::shared_ptr<Objects::Components::AudioSource>> sources;
            std::unordered_map<int, std::shared_ptr<Objects::Components::AudioListener>> listeners;

            void OnComponentAdded(Worlds::World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component, bool cloned) override;
            void OnComponentRemoved(Worlds::World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component) override;
            void OnPlay(Worlds::World& world) override;
    };

    /// Declares to the world what the audio adds to it : its component type and its world extension.
    void RegisterAudioModule();
}
