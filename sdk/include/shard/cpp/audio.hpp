// Shard C++ SDK : sounds (shard_audio.h). An actor with an "audio" component plays and pauses its clip with Actor::PlayAudio, PauseAudio and
// SetAudioVolume. A one shot sound needs no actor.
//
//   shard::Audio::PlayOneShot("sounds/shot.wav", Owner().WorldPosition());
#pragma once

#include <string_view>

#include <shard/shard_audio.h>
#include <shard/cpp/actor.hpp>

namespace shard {

    class Audio {
    public:
        /// Plays a clip of the project ("sounds/shot.wav") once, from `position` in the world when `spatial` (louder and panned by the distance
        /// and direction to the listener), flat otherwise. The sound removes itself when it ends. Throws shard::Error if there is no such clip.
        static void PlayOneShot(std::string_view clip, Vec3 position, float volume = 1.f, bool spatial = true)
        {
            float p[3];
            detail::ToArray(position, p);
            detail::Check(detail::AudioApi().play_one_shot(Str(clip), p, volume, spatial ? 1 : 0), "sound '" + std::string(clip) + "'");
        }
        /// A flat sound, with no place
        static void PlayOneShot(std::string_view clip, float volume = 1.f) { PlayOneShot(clip, Vec3{}, volume, false); }
    };
}
