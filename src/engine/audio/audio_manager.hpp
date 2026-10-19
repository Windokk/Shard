#pragma once 

#include <unordered_map>
#include <string>
#include <iostream>
#include <memory>


#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"
#include "audioID.hpp"
#include "audio_effects.hpp"

// miniaudio types stay opaque here so its (huge) header is only pulled into audio_manager.cpp
struct ma_engine;
struct ma_sound;
struct ma_decoder;

namespace Shard::Engine::Audio
{
    class SoundAsset;
    struct ReverbBusNode;

    /// What a source tells the audio about itself, besides where it is and how loud
    struct SoundParams{
        glm::dvec3 worldPosition = glm::dvec3(0.0);     // root frame, exact : the doppler effect is made of its small changes
        float dopplerLevel = 1.0f;
        float reverbSend = 1.0f;
    };

    /// Where the world is heard from, as the audio manager needs it each frame
    struct ListenerState{
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec2 facing = glm::vec2(0.0f, -1.0f);      // normalized, on the ground plane
        glm::dvec3 worldPosition = glm::dvec3(0.0);     // root frame
        glm::dvec3 velocity = glm::dvec3(0.0);          // world units per second
        float dopplerFactor = 1.0f;
        float speedOfSound = 343.0f;
    };

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

        // Splits the sound in its direct part (to the output) and its send to the reverb bus. Null when there is no reverb bus.
        void* splitter = nullptr;      // a ma_splitter_node
        float reverbSend = 1.0f;

        // Doppler : the velocity is worked out from the positions of the audio ticks, the pitch follows it smoothly
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

            /// @param silent  no audio device is opened (headless runs : a server, the tests, the CI have none) : sounds are not
            /// created, everything else behaves as usual
            void Init(float masterVolume, bool silent = false);
            void Shutdown();
            void Tick();
            void CreateSound(AudioID id, const std::string& pathInProject, glm::vec3 pos, bool spatialize);
            void RemoveSound(AudioID id);
            void PlaySound(AudioID id, float volume);
            void PauseSound(AudioID id);
            void UpdateSound(AudioID id, glm::vec3 pos, float volume, const SoundParams& params);
            void SetSpatialize(AudioID id, bool spatialize);
            void Update(const ListenerState& listener, float maxDistance);

            /// The reverb the listener is in this frame (nothing covered : no reverb). The bus moves toward it smoothly.
            void SetReverb(const ReverbMix& mix);
            int GetSoundsCount() { return soundCount; }

        private:

            void InitReverbBus();
            void ApplyReverb();

            ma_engine* engine = nullptr;
            int soundCount = 0;
            float masterVolume = 100.0f;

            // Seconds between this audio tick and the last one (real time : the sound does not slow down with the game)
            float tickDelta = 0.0f;
            unsigned long long tickIndex = 0;
            double lastTickTime = -1.0;
            std::unordered_map<int, glm::dvec3> previousListenerPositions;
            std::unordered_map<int, glm::dvec3> listenerVelocities;

            // The reverb every sound's send goes into, shared by all of them
            ReverbBusNode* reverbBus = nullptr;
            ReverbMix reverbTarget;
            ReverbParams reverbCurrent;
            float reverbCoverage = 0.0f;
    };

}