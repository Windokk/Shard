#pragma once 

#include <unordered_map>
#include <string>
#include <iostream>


#include <glm/glm.hpp>
#include <fmod.hpp>

#include "engine/assets/vfs/filesystem.hpp"
#include "audioID.hpp"

namespace Shard::Engine::Audio
{
    struct Sound{
        FMOD_SOUND* fmod_sound;
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
            int GetSoundsCount() { return channels.size(); }

        private:

            FMOD_SYSTEM *system;
            std::unordered_map<std::string, FMOD_CHANNEL*> channels;
            FMOD_CREATESOUNDEXINFO exinfo;
            float masterVolume = 100.0f;
    };

}