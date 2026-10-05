#include "audio_source.hpp"

#include "engine/world/actor.hpp"

#include "engine/core/engine.hpp"

#include "engine/filesystem/assetID.hpp"

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

                // CreateSound silently no-ops (logging an error) if the file couldn't be loaded -
                // only adopt the new ID once it's actually backed by a registered sound, otherwise
                // audioID would be left "valid" while pointing at nothing, crashing the next Update().
                if(GetEngineContext()->GetAudioIDManager()->GetSoundFromID(newID))
                    audioID = newID;
            }
        }
    }

    void AudioSource::Deserialize(const json componentData)
    {
        auto getFloat = [&](const char* key, float defaultValue = 0.0f) -> float
        {
            if (!componentData.contains(key) || !componentData[key].is_number())
                return defaultValue;
            return componentData[key].get<float>();
        };

        auto getBool = [&](const char* key, bool defaultValue) -> bool
        {
            if (!componentData.contains(key) || !componentData[key].is_boolean())
                return defaultValue;
            return componentData[key].get<bool>();
        };

        auto getString = [&](const char* key, const std::string& defaultValue = "") -> std::optional<std::string>
        {
            if (!componentData.contains(key) || !componentData[key].is_string())
                return std::nullopt;
            return componentData[key].get<std::string>();
        };

        auto volume = getFloat("volume", 1.0f);

        auto soundOpt = getString("sound");
        if (!soundOpt)
        {
            DEBUG_ERROR("AudioSource missing or invalid 'sound'");
            return;
        }

        playOnStart = getBool("playOnStart", true);
        spatialize = getBool("spatialize", true);

        SetSound(GetEngineContext()->GetAssetIDManager()->GetIDFromNameInProject(*soundOpt));
        SetVolume(volume);

        bool active = getBool("active", false);

        if (active)
            Activate();
        else
            DeActivate();
    }

    ordered_json AudioSource::Serialize()
    {
        ordered_json comp;

        comp["type"] = "audio";

        comp["active"] = activated;

        comp["volume"] = volume;

        comp["playOnStart"] = playOnStart;

        comp["spatialize"] = spatialize;

        auto asset = GetEngineContext()->GetAssetIDManager()->GetAssetFromID(assetID);
        comp["sound"] = asset ? asset->baseInfos.nameInProject : "";

        return comp;
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