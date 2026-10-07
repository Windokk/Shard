#include "audio_manager.hpp"

#include <cassert>

#include <miniaudio/miniaudio.h>

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

        engine = new ma_engine();
        ma_result result = ma_engine_init(nullptr, engine);
        if (result != MA_SUCCESS) {
            DEBUG_ERROR(std::string("ma_engine_init failed: ") + ma_result_description(result));
            delete engine;
            engine = nullptr;
        }
    }

    void AudioManager::CreateSound(AudioID id, const std::string& pathInProject, glm::vec3 pos, bool spatialize)
    {
        if (!engine) {
            return;
        }

        std::shared_ptr<SoundAsset> soundAsset = Core::GetEngine().GetResourcesManager()->Get<Audio::SoundAsset>(Core::Resources::AssetKind::Sound, pathInProject);
        if (!soundAsset) {
            return;
        }

        const std::string& file = soundAsset->GetBuffer();

        auto sound = new Sound();
        sound->asset = soundAsset;
        sound->decoder = new ma_decoder();
        sound->sound = new ma_sound();

        // The engine mixes in f32 : decode to it and keep the file's own channel count / sample rate,
        // the engine node converts both.
        ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 0, 0);
        ma_result result = ma_decoder_init_memory(file.data(), file.size(), &decoderConfig, sound->decoder);
        if (result != MA_SUCCESS) {
            DEBUG_ERROR("miniaudio error decoding sound '" + pathInProject + "' (" + std::to_string(file.size()) + " bytes): " + ma_result_description(result));
            delete sound->decoder;
            delete sound->sound;
            delete sound;
            return;
        }

        // Positioning is done by Update() (pan + linear attenuation), so miniaudio's own 3D is off
        result = ma_sound_init_from_data_source(engine, sound->decoder, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, sound->sound);
        if (result != MA_SUCCESS) {
            DEBUG_ERROR("miniaudio error creating sound '" + pathInProject + "': " + ma_result_description(result));
            ma_decoder_uninit(sound->decoder);
            delete sound->decoder;
            delete sound->sound;
            delete sound;
            return;
        }

        sound->pos = pos;
        sound->spatialize = spatialize;

        Core::GetEngine().GetAudioIDManager()->AssignID(id, sound);
        soundCount++;
    }

    static void DestroySound(Sound* sound)
    {
        ma_sound_uninit(sound->sound);
        ma_decoder_uninit(sound->decoder);
        delete sound->sound;
        delete sound->decoder;
        delete sound;
    }

    void AudioManager::RemoveSound(AudioID id)
    {
        Sound* sound = Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id);
        if (!sound)
            return;

        DestroySound(sound);
        Core::GetEngine().GetAudioIDManager()->DestroyID(id);
        soundCount--;
    }

    void AudioManager::PlaySound(AudioID id, float volume)
    {
        Sound* sound = Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id);
        if(sound && !sound->isPlaying){
            ma_sound_seek_to_pcm_frame(sound->sound, 0);
            ma_sound_start(sound->sound);
            sound->isPlaying = true;
            sound->initialVolume = volume/100.0f;
        }
    }

    void AudioManager::PauseSound(AudioID id)
    {
        Sound* sound = Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id);
        if(sound && sound->isPlaying){
            ma_sound_stop(sound->sound);
            sound->isPlaying = false;
        }
    }

    void AudioManager::UpdateSound(AudioID id, glm::vec3 pos, float volume)
    {
        Sound* sound = Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id);
        if(!sound)
            return;

        sound->pos = pos;
        sound->initialVolume = volume;
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

            // Reached the end on its own (the audio thread never touches our state, it is polled here)
            if(ma_sound_at_end(pair.second->sound)){
                pair.second->isPlaying = false;
                continue;
            }

            if(pair.second->spatialize){
                float pan = glm::sin(glm::orientedAngle(glm::normalize(glm::vec2(pair.second->pos.x - listenerPos.x, pair.second->pos.z - listenerPos.z)), listenerFacingNormalized));
                if (pan < -1.0f) pan = -1.0f;
                if (pan > 1.0f) pan = 1.0f;
                float volume = 1.0f - (glm::distance(listenerPos, pair.second->pos) / maxDistance);
                volume *= masterVolume/100;
                if (volume < 0.0f) volume = 0.0f;
                if (volume > 1.0f) volume = 1.0f;

                volume *= pair.second->initialVolume;

                ma_sound_set_pan(pair.second->sound, -1 * pan);
                ma_sound_set_volume(pair.second->sound, volume);
            }
            else{
                // Ambient/music sources skip positional pan+attenuation entirely - flat volume,
                // native stereo image left untouched (never panned away from center).
                float volume = masterVolume/100 * pair.second->initialVolume;
                if (volume < 0.0f) volume = 0.0f;
                if (volume > 1.0f) volume = 1.0f;

                ma_sound_set_volume(pair.second->sound, volume);
            }
        }
    }

    void AudioManager::Shutdown()
    {
        for (const auto& pair : Core::GetEngine().GetAudioIDManager()->GetAudioMap()) {
            DestroySound(pair.second);
            Core::GetEngine().GetAudioIDManager()->DestroyID(pair.first);
        }
        soundCount = 0;

        if (engine) {
            ma_engine_uninit(engine);
            delete engine;
            engine = nullptr;
        }
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