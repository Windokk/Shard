#pragma once

#include <memory>
#include <unordered_map>

#include "engine/world/world_extension.hpp"

namespace Shard::Engine::Objects::Components{
    class AudioSource;
    class AudioListener;
    class ReverbZone;
}

namespace Shard::Engine::Audio{

    class AudioWorldData : public Worlds::IWorldExtension{
        public:
            std::unordered_map<int, std::shared_ptr<Objects::Components::AudioSource>> sources;
            std::unordered_map<int, std::shared_ptr<Objects::Components::AudioListener>> listeners;
            std::unordered_map<int, std::shared_ptr<Objects::Components::ReverbZone>> reverbZones;

            void OnComponentAdded(Worlds::World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component, bool cloned) override;
            void OnComponentRemoved(Worlds::World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component) override;
            void OnPlay(Worlds::World& world) override;
    };

    void RegisterAudioModule();
}
