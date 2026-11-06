#pragma once 

#include <unordered_map>
#include <string>
#include <iostream>
#include <memory>
#include <vector>


#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"
#include "audioID.hpp"
#include "audio_effects.hpp"

struct ma_engine;
struct ma_sound;
struct ma_decoder;

namespace Shard::Engine::Audio
{
    class SoundAsset;
    struct ReverbBusNode;

    struct SoundParams{
        glm::dvec3 worldPosition = glm::dvec3(0.0);
        float dopplerLevel = 1.0f;
        float reverbSend = 1.0f;
    };

    struct ListenerState{
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec2 facing = glm::vec2(0.0f, -1.0f);
        glm::dvec3 worldPosition = glm::dvec3(0.0);
        glm::dvec3 velocity = glm::dvec3(0.0);
        float dopplerFactor = 1.0f;
        float speedOfSound = 343.0f;
    };

    struct Sound{
        // Heap-allocated because miniaudio objects must not move once initialised. The decoder reads the asset's bytes directly, so the asset lives as long as the sound.
        ma_decoder* decoder = nullptr;
        ma_sound* sound = nullptr;
        std::shared_ptr<SoundAsset> asset;
        glm::vec3 pos;
        bool isPlaying = false;
        float initialVolume;
        bool spatialize = true;

        void* splitter = nullptr;
        float reverbSend = 1.0f;

        float dopplerLevel = 1.0f;
        glm::dvec3 worldPos = glm::dvec3(0.0);
        glm::dvec3 prevWorldPos = glm::dvec3(0.0);
        glm::dvec3 velocity = glm::dvec3(0.0);
        bool hasPrevPos = false;
        unsigned long long velocityTick = 0;
        float pitch = 1.0f;
    };

    class AudioManager{
        public:

            void Init(float masterVolume, bool silent = false);
            void Shutdown();
            void Tick();
            void CreateSound(AudioID id, const std::string& pathInProject, glm::vec3 pos, bool spatialize);
            void RemoveSound(AudioID id);
            void PlaySound(AudioID id, float volume);
            void PauseSound(AudioID id);
            void UpdateSound(AudioID id, glm::vec3 pos, float volume, const SoundParams& params);
            void SetSpatialize(AudioID id, bool spatialize);

            bool PlayOneShot(const std::string& pathInProject, glm::vec3 pos, float volume, bool spatialize);
            void Update(const ListenerState& listener, float maxDistance);

            void SetReverb(const ReverbMix& mix);
            int GetSoundsCount() { return soundCount; }

        private:

            void InitReverbBus();
            void ApplyReverb();

            ma_engine* engine = nullptr;
            int soundCount = 0;
            float masterVolume = 100.0f;

            std::vector<AudioID> oneShots;

            float tickDelta = 0.0f;
            unsigned long long tickIndex = 0;
            double lastTickTime = -1.0;
            std::unordered_map<int, glm::dvec3> previousListenerPositions;
            std::unordered_map<int, glm::dvec3> listenerVelocities;

            ReverbBusNode* reverbBus = nullptr;
            ReverbMix reverbTarget;
            ReverbParams reverbCurrent;
            float reverbCoverage = 0.0f;
    };

}