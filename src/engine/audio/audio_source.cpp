#include "audio_source.hpp"

#include "engine/world/actor.hpp"

#include "engine/world/engine.hpp"

#include "engine/assets/assetID.hpp"

#include "audio_source.reflection.hpp"

namespace Shard::Engine::Objects::Components{
    
    AudioSource::AudioSource(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {
    }

    void AudioSource::Update()
    {
        if(audioID.IsValid() && GetEngineContext()->GetAudioIDManager()->GetSoundFromID(audioID) && activated){
            GetEngineContext()->GetAudioManager()->UpdateSound(audioID, parent->transform->GetWorldPosition(), volume);
        }
        else{
            if(!pathInProject.empty() && volume != -1.0f){
                Audio::AudioID newID = GetEngineContext()->GetAudioIDManager()->GenerateNewID();
                GetEngineContext()->GetAudioManager()->CreateSound(newID, pathInProject, parent->transform->GetWorldPosition(), spatialize);

                // CreateSound silently no-ops (logging an error) if the file couldn't be loaded
                if(GetEngineContext()->GetAudioIDManager()->GetSoundFromID(newID))
                    audioID = newID;
            }
        }
    }

    void AudioSource::Deserialize(const json componentData)
    {
        json data = componentData;

        // Levels saved before the serialization went through the reflection call the sound's field "sound"
        if (data.is_object() && !data.contains("assetID") && data.contains("sound"))
            data["assetID"] = data["sound"];

        DeserializeReflected(data, false);
    }

    ordered_json AudioSource::Serialize()
    {
        return SerializeReflected("audio");
    }

    void AudioSource::Destroy()
    {
        RemoveSound();
    }

    std::shared_ptr<Component> AudioSource::Clone() const
    {
        auto cloned = Object::Create<AudioSource>(*this);

        return cloned;
    }

    void AudioSource::SetSound(Filesystem::AssetID soundID)
    {
        this->assetID = soundID;

        auto asset = GetEngineContext()->GetAssetIDManager()->GetAssetFromID(soundID);
        if (asset)
        {
            this->pathInProject = asset->baseInfos.nameInProject;

            if (audioID.IsValid())
            {
                RemoveSound();
                audioID = Audio::AudioID();
            }
        }

        Update();
    }

    void AudioSource::SetVolume(float volume)
    {
        this->volume = volume;
        Update();
    }

    void AudioSource::Play()
    {
        if(!activated)
            return;

        if(!audioID.IsValid() || !GetEngineContext()->GetAudioIDManager()->GetSoundFromID(audioID))
            return;

        GetEngineContext()->GetAudioManager()->PlaySound(this->audioID, this->volume);
    }

    void AudioSource::Pause()
    {
        if(!audioID.IsValid() || !GetEngineContext()->GetAudioIDManager()->GetSoundFromID(audioID))
            return;

        GetEngineContext()->GetAudioManager()->PauseSound(this->audioID);
    }

    void AudioSource::RemoveSound()
    {
        if(!audioID.IsValid() || !GetEngineContext()->GetAudioIDManager()->GetSoundFromID(audioID))
            return;

        GetEngineContext()->GetAudioManager()->RemoveSound(this->audioID);
    }

    void AudioSource::OnFieldChanged(const FieldChangedEvent &event)
    {
        if(event.field->name == "volume"){
            SetVolume(volume);
        }
        else if(event.field->name == "spatialize"){
            if(audioID.IsValid())
                GetEngineContext()->GetAudioManager()->SetSpatialize(audioID, spatialize);
        }
        else if(event.field->type == TypeID::Asset){
            SetSound(assetID);
        }
    }

    void AudioSource::OnPlay(){
        if(playOnStart){
            Play();
        }
    }
}