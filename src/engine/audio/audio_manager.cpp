#include "audio_manager.hpp"

#include <cassert>

#include <fmod_errors.h>

#include "engine/world/actor.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/core/color.hpp"
#include "engine/world/time_manager.hpp"
#include "engine/world/engine.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/core/diagnostics/profiler.hpp"

#include "engine/audio/audio_source.hpp"
#include "engine/audio/audio_listener.hpp"
#include "engine/audio/audio_world_data.hpp"
#include "engine/assets/resources_manager.hpp"
#include "sound_asset.hpp"


namespace Shard::Engine::Audio
{
    using namespace Filesystem;

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

        {
            Core::Resources::AssetKindInfo info;
            info.load = [](const std::string&, const Filesystem::AssetInfos& infos) -> std::shared_ptr<void> {
                const Filesystem::Path& path = infos.baseInfos.path;
                if (!path.Exists())
                {
                    DEBUG_ERROR("Couldn't load sound: " + path.full);
                    return nullptr;
                }

                std::shared_ptr<SoundAsset> sound = std::make_shared<SoundAsset>();
                sound->SetBuffer(path.ReadFile());
                return sound;
            };
            info.setAssetID = [](void* resource, Filesystem::AssetID id){ static_cast<SoundAsset*>(resource)->SetAssetID(id); };
            // The (multi-megabyte) file read moves off the main thread, so the first Play() of an
            // AudioSource finds the bytes already cached instead of blocking on the disk.
            info.prefetch = [](Core::Resources::ResourcesManager& resources, const std::string& pathInProject, const Filesystem::Path& file) -> std::function<void()> {
                auto bytes = std::make_shared<std::string>(file.ReadFile());
                if(bytes->empty())
                    return nullptr;

                return [&resources, pathInProject, bytes](){
                    std::shared_ptr<SoundAsset> sound = std::make_shared<SoundAsset>();
                    sound->SetBuffer(std::move(*bytes));
                    resources.Adopt(Core::Resources::AssetKind::Sound, pathInProject, sound);
                };
            };
            Core::GetEngine().GetResourcesManager()->RegisterKind(Core::Resources::AssetKind::Sound, std::move(info));
        }

        if (Debugging::Profiler* profiler = Debugging::Profiler::Active())
        {
            profiler->AddStatsProvider([this](Debugging::MinimalStatistics& stats)
            {
                stats.sounds = GetSoundsCount();
            });
        }

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
        std::shared_ptr<SoundAsset> soundAsset = Core::GetEngine().GetResourcesManager()->Get<Audio::SoundAsset>(Core::Resources::AssetKind::Sound, pathInProject);
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
        if(int worldCount = Core::GetEngine().GetWorldManager()->GetLoadedWorldCount() > 0){
            for(int i = 0; i < worldCount; i++){
                for(auto& [id,source] : Core::GetEngine().GetWorldManager()->GetWorldAt(i)->Ext<AudioWorldData>().sources){
                    source->Update();
                }

                // The world is heard from its first active listener
                std::shared_ptr<Objects::Components::AudioListener> listener;
                for(auto& [id,candidate] : Core::GetEngine().GetWorldManager()->GetWorldAt(i)->Ext<AudioWorldData>().listeners){
                    if(candidate->Active()){
                        listener = candidate;
                        break;
                    }
                }

                if(listener == nullptr)
                    return;

                const glm::vec3 position = listener->parent->transform->GetWorldPosition();
                const glm::vec3 forward = listener->parent->transform->GetWorldForward();

                AudioManager::Update(position, glm::normalize(glm::vec2(forward.x, forward.z)), 100.0f);
                
            }
            
        }
    }
}