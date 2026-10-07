#pragma once 

#include <unordered_map>
#include <string>
#include <iostream>
#include <memory>


#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"
#include "audioID.hpp"

// miniaudio types stay opaque here so its (huge) header is only pulled into audio_manager.cpp
struct ma_engine;
struct ma_sound;
struct ma_decoder;

namespace Shard::Engine::Audio
{
    class SoundAsset;

    struct Sound{
        // Heap-allocated because miniaudio objects must not move once initialised. The decoder reads
        // straight from the asset's byte buffer, so the asset is kept alive for as long as the sound.
        ma_decoder* decoder = nullptr;
        ma_sound* sound = nullptr;
        std::shared_ptr<SoundAsset> asset;
        glm::vec3 pos;
        bool isPlaying = false;
        float initialVolume;
        // When false, skips the per-frame distance/angle pan+attenuation pass entirely (Update())
        // and just plays at flat volume in its native stereo image - for ambient/music sources
        // rather than positional sound effects.
        bool spatialize = true;
    };

    class AudioManager{
        public:

            void Init(float masterVolume);
            void Shutdown();
            void Tick();
            void CreateSound(AudioID id, const std::string& pathInProject, glm::vec3 pos, bool spatialize);
            void RemoveSound(AudioID id);
            void PlaySound(AudioID id, float volume);
            void PauseSound(AudioID id);
            void UpdateSound(AudioID id, glm::vec3 pos, float volume);
            void SetSpatialize(AudioID id, bool spatialize);
            void Update(glm::vec3 listenerPos, glm::vec2 listenerFacingNormalized, float maxDistance);
            int GetSoundsCount() { return soundCount; }

        private:

            ma_engine* engine = nullptr;
            int soundCount = 0;
            float masterVolume = 100.0f;
    };

}