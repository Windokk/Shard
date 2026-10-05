#pragma once

#include "engine/audio/audio_manager.hpp"

#include "engine/world/components/component.hpp"

#include "engine/renderer/utils.hpp"

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
                // instant a level loads/deserializes in the editor. Actual playback is gated behind
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

            FIELD(Editable)
            Filesystem::AssetID assetID;

            FIELD(Editable)
            float volume = 1.0f;

            FIELD(Editable)
            bool playOnStart = true;

            FIELD(Editable)
            bool spatialize = true;

        private:
            std::string pathInProject;
            Audio::AudioID audioID;
    };
}