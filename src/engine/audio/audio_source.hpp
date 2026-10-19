#pragma once

#include "engine/audio/audio_manager.hpp"

#include "engine/world/components/component.hpp"

#include "engine/core/color.hpp"

#include "engine/assets/reflection/attributes.hpp"

namespace Shard::Engine::Objects::Components
{
    class CLASS() AudioSource : public Component {

        public:
            AudioSource(std::shared_ptr<Actor> parent, uint32_t local_id);

            void SetSound(Filesystem::AssetID soundID);
            void SetVolume(float volume);
            void Play();
            void Pause();
            void RemoveSound();
            void Update();

            void Deserialize(const json componentData) override;

            ordered_json Serialize() override;

            void Destroy() override;

            void Activate() override
            {
                Component::Activate();

                // Enabling the component only prepares the underlying sound (Update() creates it if
                // needed) - it must not start audible playback on its own, since that would fire the
                // instant a world loads/deserializes in the editor. Actual playback is gated behind
                // OnPlay()/playOnStart, invoked when the game actually enters Play mode.
                Update();
            }

            void DeActivate() override
            {
                Component::DeActivate();

                Pause();
            }

            std::shared_ptr<Component> Clone() const override;

            void OnFieldChanged(const FieldChangedEvent& event) override;
            
            void OnPlay();

            DECLARE_DESCRIPTOR(AudioSource)

            FIELD(Editable, tooltip=The audio clip to play)
            Filesystem::AssetID assetID;

            FIELD(Editable, tooltip=Playback volume, 1 is the clip at its original level)
            float volume = 1.0f;

            FIELD(Editable, tooltip=Start playing as soon as the game starts)
            bool playOnStart = true;

            FIELD(Editable, tooltip=Play in 3D : the volume and panning follow the distance and direction to the listener)
            bool spatialize = true;

            FIELD(Editable, range=0.0f|5.0f, tooltip=Doppler effect : how much the pitch follows the movement of the source and of the listener. 0 is off, 1 is physical, more exaggerates it)
            float dopplerLevel = 1.0f;

            FIELD(Editable, range=0.0f|1.0f, tooltip=How much of the sound goes through the reverb of the zone the listener is in. 0 keeps it dry)
            float reverbSend = 1.0f;

        private:
            Audio::SoundParams MakeSoundParams() const;

            std::string pathInProject;
            Audio::AudioID audioID;
    };
}