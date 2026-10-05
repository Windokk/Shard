#include "audio_manager.hpp"

#include <cassert>

#include <fmod_errors.h>

#include "engine/world/actor.hpp"
#include "engine/world/levels/level_manager.hpp"
#include "engine/renderer/utils.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/frontend/camera_manager.hpp"
#include "engine/time/time_manager.hpp"
#include "engine/core/engine.hpp"
#include "engine/debugging/logger.hpp"

#include "engine/audio/audio_source.hpp"
#include "engine/core/resources/resources_manager.hpp"
#include "sound_asset.hpp"


namespace Shard::Engine::Audio
{
    using namespace Filesystem;
    using namespace Rendering;

    FMOD_RESULT F_CALL OnSoundStopped(FMOD_CHANNELCONTROL* chanControl,
                                      FMOD_CHANNELCONTROL_TYPE controlType,
                                      FMOD_CHANNELCONTROL_CALLBACK_TYPE callbackType,
                                      void* commandData1,
                                      void* commandData2)
    {
        if (callbackType == FMOD_CHANNELCONTROL_CALLBACK_END) {
            void* userData = nullptr;
            FMOD_Channel_GetUserData((FMOD_CHANNEL*)chanControl, &userData);

            if (userData) {
                AudioID* id = static_cast<AudioID*>(userData);

                auto* sound = Core::GetEngine().GetAudioIDManager()->GetSoundFromID(*id);
                if (sound) {
                    sound->isPlaying = false;
                }

                delete id;
            }
        }

        return FMOD_OK;
    }

    void AudioManager::Init(float masterVolume)
    {
        AudioManager::masterVolume = masterVolume;

        // Initialize FMOD system
        FMOD_RESULT result = FMOD_System_Create(&system, FMOD_VERSION);
        if (result != FMOD_OK) {
            DEBUG_ERROR(std::string("FMOD_System_Create failed: ") + FMOD_ErrorString(result));
            return;
        }

        result = FMOD_System_Init(system, 512, FMOD_INIT_NORMAL, 0);
        if (result != FMOD_OK) {
            DEBUG_ERROR(std::string("FMOD_System_Init failed: ") + FMOD_ErrorString(result));
        }
    }

    void AudioManager::CreateSound(AudioID id, const std::string& pathInProject, glm::vec3 pos, bool spatialize)
    {
        std::shared_ptr<SoundAsset> soundAsset = Core::GetEngine().GetResourcesManager()->GetSound(pathInProject);
        if (!soundAsset) {
            return;
        }

        auto sound = new Sound();
        FMOD_CHANNEL* channel = nullptr;

        const std::string& file = soundAsset->GetBuffer();
        const char* buffer = file.data();
        size_t buffer_size = file.size();

        FMOD_CREATESOUNDEXINFO exinfo{};
        exinfo.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
        exinfo.length = buffer_size;

        FMOD_RESULT result = FMOD_System_CreateSound(system, buffer, FMOD_2D | FMOD_OPENMEMORY, &exinfo, &sound->fmod_sound);
        if (result != FMOD_OK) {
            DEBUG_ERROR("FMOD error creating sound '" + pathInProject + "' (" + std::to_string(buffer_size) + " bytes): " + FMOD_ErrorString(result));
            delete sound;
            return;
        }

        sound->pos = pos;
        sound->spatialize = spatialize;

        Core::GetEngine().GetAudioIDManager()->AssignID(id, sound);
        channels.emplace(id.GetAsString() + "_channel", channel);
    }

    void AudioManager::RemoveSound(AudioID id)
    {
        Sound* sound = Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id);
        FMOD_Sound_Release(sound->fmod_sound);
        delete sound;
        Core::GetEngine().GetAudioIDManager()->DestroyID(id);
        channels.erase(id.GetAsString()+"_channel");
    }

    void AudioManager::PlaySound(AudioID id, float volume)
    {
        if(!Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id)->isPlaying){
            FMOD_System_PlaySound(system, Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id)->fmod_sound, nullptr, true, &channels.at(id.GetAsString()+"_channel"));
            AudioID* idCopy = new AudioID(id);
            FMOD_Channel_SetUserData(channels.at(id.GetAsString()+"_channel"), idCopy);
            FMOD_Channel_SetCallback(channels.at(id.GetAsString()+"_channel"), OnSoundStopped);
            FMOD_Channel_SetPaused(channels.at(id.GetAsString()+"_channel"), false);
            Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id)->isPlaying = true;
            Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id)->initialVolume = volume/100.0f;
        }
    }

    void AudioManager::PauseSound(AudioID id)
    {
        if(Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id)->isPlaying){
            FMOD_Channel_SetPaused(channels.at(id.GetAsString()+"_channel"), true);
            Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id)->isPlaying = false;
        }
    }

    void AudioManager::UpdateSound(AudioID id, glm::vec3 pos, float volume)
    {
        Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id)->pos = pos;
        Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id)->initialVolume = volume;
    }

    void AudioManager::SetSpatialize(AudioID id, bool spatialize)
    {
        Sound* sound = Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id);
        if(sound)
            sound->spatialize = spatialize;
    }

    void AudioManager::Update(glm::vec3 listenerPos, glm::vec2 listenerFacingNormalized, float maxDistance)
    {
        if(maxDistance <= 0){
            DEBUG_FATAL("maxDistance can't be >= 0");
        }

        for (const auto& pair : Core::GetEngine().GetAudioIDManager()->GetAudioMap()) {
            if(!pair.second->isPlaying)
                continue;

            const std::string& channelKey = pair.first.GetAsString()+"_channel";

            if(pair.second->spatialize){
                float pan = glm::sin(glm::orientedAngle(glm::normalize(glm::vec2(pair.second->pos.x - listenerPos.x, pair.second->pos.z - listenerPos.z)), listenerFacingNormalized));
                if (pan < -1.0f) pan = -1.0f;
                if (pan > 1.0f) pan = 1.0f;
                float volume = 1.0f - (glm::distance(listenerPos, pair.second->pos) / maxDistance);
                volume *= masterVolume/100;
                if (volume < 0.0f) volume = 0.0f;
                if (volume > 1.0f) volume = 1.0f;

                volume *= pair.second->initialVolume;

                FMOD_Channel_SetPan(channels.at(channelKey), -1 * pan);
                FMOD_Channel_SetVolume(channels.at(channelKey), volume);
            }
            else{
                // Ambient/music sources skip positional pan+attenuation entirely - flat volume,
                // native stereo image left untouched (never panned away from center).
                float volume = masterVolume/100 * pair.second->initialVolume;
                if (volume < 0.0f) volume = 0.0f;
                if (volume > 1.0f) volume = 1.0f;

                FMOD_Channel_SetVolume(channels.at(channelKey), volume);
            }
        }

        FMOD_System_Update(system);
    }

    void AudioManager::Shutdown()
    {
        for (const auto& pair : Core::GetEngine().GetAudioIDManager()->GetAudioMap()) {
            Core::GetEngine().GetAudioIDManager()->DestroyID(pair.first); 
        }

        FMOD_System_Close(system);
        FMOD_System_Release(system);
    }

    void AudioManager::Tick()
    {
        if(int levelCount = Core::GetEngine().GetLevelManager()->GetLoadedLevelCount() > 0){
            for(int i = 0; i < levelCount; i++){
                for(auto& [id,source] : Core::GetEngine().GetLevelManager()->GetLevelAt(i)->audioSources){
                    source->Update();
                }

                std::shared_ptr<Objects::Components::Camera> cam = Core::GetEngine().GetCameraManager()->GetActiveCamera();

                if(cam == nullptr)
                    return;

                AudioManager::Update(cam->parent->transform->GetWorldPosition(), glm::normalize(glm::vec2(cam->parent->transform->GetWorldForward().x, cam->parent->transform->GetWorldForward().z)), 100.0f);
                
            }
            
        }
    }
}