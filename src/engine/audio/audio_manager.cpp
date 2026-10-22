#include "audio_manager.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <vector>

#include <miniaudio/miniaudio.h>
#include <miniaudio/extras/nodes/ma_reverb_node/verblib.h>

#include "engine/world/actor.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/core/color.hpp"
#include "engine/world/time_manager.hpp"
#include "engine/world/engine.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/core/diagnostics/profiler.hpp"

#include "engine/audio/audio_source.hpp"
#include "engine/audio/audio_listener.hpp"
#include "engine/audio/reverb_zone.hpp"
#include "engine/audio/audio_world_data.hpp"
#include "engine/assets/resources_manager.hpp"
#include "sound_asset.hpp"


namespace Shard::Engine::Audio
{
    using namespace Filesystem;

    /// The reverb every sound sends to : a low cut and a high cut, then a pre-delay, then the Freeverb of verblib. The wet signal only,
    /// the direct sound goes to the output on its own (see Sound::splitter). The settings are read from the audio thread, they are set
    /// from the main one through atomics.
    struct ReverbBusNode
    {
        ma_node_base base;      // must stay first
        verblib reverb;

        ma_uint32 channels = 2;
        ma_uint32 sampleRate = 48000;

        std::atomic<float> roomSize{0.5f};
        std::atomic<float> damping{0.25f};
        std::atomic<float> width{1.0f};
        std::atomic<float> preDelayMs{0.0f};
        std::atomic<float> lowCutHz{20.0f};
        std::atomic<float> highCutHz{20000.0f};

        // Audio thread only
        float appliedRoomSize = -1.0f;
        float appliedDamping = -1.0f;
        float appliedWidth = -1.0f;
        std::vector<float> delayLine;       // interleaved, delayCapacity frames
        ma_uint32 delayCapacity = 0;
        ma_uint32 writePosition = 0;
        float lowPass[2] = {0.0f, 0.0f};
        float highPassLow[2] = {0.0f, 0.0f};
    };

    static constexpr float kMaxPreDelayMs = 250.0f;

    static void ReverbBusProcess(ma_node* node, const float** framesIn, ma_uint32* frameCountIn, float** framesOut, ma_uint32* frameCountOut)
    {
        (void)frameCountIn;
        ReverbBusNode* bus = reinterpret_cast<ReverbBusNode*>(node);

        const float room = bus->roomSize.load(std::memory_order_relaxed);
        const float damp = bus->damping.load(std::memory_order_relaxed);
        const float width = bus->width.load(std::memory_order_relaxed);
        if (room != bus->appliedRoomSize) { verblib_set_room_size(&bus->reverb, room); bus->appliedRoomSize = room; }
        if (damp != bus->appliedDamping) { verblib_set_damping(&bus->reverb, damp); bus->appliedDamping = damp; }
        if (width != bus->appliedWidth) { verblib_set_width(&bus->reverb, width); bus->appliedWidth = width; }

        const float sampleRate = static_cast<float>(bus->sampleRate);
        const float twoPi = 6.28318530718f;
        const float lowPassCoef = 1.0f - std::exp(-twoPi * std::clamp(bus->highCutHz.load(std::memory_order_relaxed), 100.0f, sampleRate * 0.45f) / sampleRate);
        const float highPassCoef = 1.0f - std::exp(-twoPi * std::clamp(bus->lowCutHz.load(std::memory_order_relaxed), 1.0f, sampleRate * 0.45f) / sampleRate);

        const ma_uint32 delayFrames = std::min<ma_uint32>(
            static_cast<ma_uint32>(std::clamp(bus->preDelayMs.load(std::memory_order_relaxed), 0.0f, kMaxPreDelayMs) * 0.001f * sampleRate),
            bus->delayCapacity - 1);

        const ma_uint32 channels = bus->channels;
        const float* in = framesIn[0];
        float* out = framesOut[0];
        ma_uint32 remaining = *frameCountOut;

        float scratch[512 * 2];
        while (remaining > 0)
        {
            const ma_uint32 chunk = std::min<ma_uint32>(remaining, 512);

            for (ma_uint32 f = 0; f < chunk; ++f)
            {
                const ma_uint32 readPosition = (bus->writePosition + bus->delayCapacity - delayFrames) % bus->delayCapacity;
                for (ma_uint32 c = 0; c < channels; ++c)
                {
                    float x = in[f * channels + c];

                    bus->lowPass[c] += lowPassCoef * (x - bus->lowPass[c]);
                    x = bus->lowPass[c];
                    bus->highPassLow[c] += highPassCoef * (x - bus->highPassLow[c]);
                    x -= bus->highPassLow[c];

                    bus->delayLine[bus->writePosition * channels + c] = x;
                    scratch[f * channels + c] = bus->delayLine[readPosition * channels + c];
                }
                bus->writePosition = (bus->writePosition + 1) % bus->delayCapacity;
            }

            verblib_process(&bus->reverb, scratch, out, chunk);

            in += chunk * channels;
            out += chunk * channels;
            remaining -= chunk;
        }
    }

    static ma_node_vtable g_reverbBusVTable =
    {
        ReverbBusProcess,
        nullptr,
        1,  // one input bus : the sends of the sounds
        1,  // one output bus
        MA_NODE_FLAG_CONTINUOUS_PROCESSING  // the tail goes on after the sounds have stopped
    };

    static void DestroyReverbBus(ReverbBusNode* bus)
    {
        if (!bus)
            return;
        ma_node_uninit(&bus->base, nullptr);
        delete bus;
    }

    void AudioManager::Init(float masterVolume, bool silent)
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

        if (silent)
            return;

        engine = new ma_engine();
        ma_result result = ma_engine_init(nullptr, engine);
        if (result != MA_SUCCESS) {
            DEBUG_ERROR(std::string("ma_engine_init failed: ") + ma_result_description(result));
            delete engine;
            engine = nullptr;
            return;
        }

        InitReverbBus();
    }

    void AudioManager::InitReverbBus()
    {
        // verblib is mono or stereo, and a node can only be connected to another of the same channel count
        const ma_uint32 channels = ma_engine_get_channels(engine);
        if (channels != 1 && channels != 2)
            return;

        ReverbBusNode* bus = new ReverbBusNode();
        bus->channels = channels;
        bus->sampleRate = ma_engine_get_sample_rate(engine);
        bus->delayCapacity = static_cast<ma_uint32>(kMaxPreDelayMs * 0.001f * bus->sampleRate) + 2;
        bus->delayLine.assign(static_cast<size_t>(bus->delayCapacity) * channels, 0.0f);

        if (verblib_initialize(&bus->reverb, bus->sampleRate, channels) == 0) {
            DEBUG_ERROR("verblib_initialize failed : reverb zones are off");
            delete bus;
            return;
        }
        // Only the reverberated signal goes out of the bus : the level is the output volume of the node (see ApplyReverb)
        verblib_set_wet(&bus->reverb, verblib_initialwet);
        verblib_set_dry(&bus->reverb, 0.0f);

        ma_node_config nodeConfig = ma_node_config_init();
        nodeConfig.vtable = &g_reverbBusVTable;
        nodeConfig.pInputChannels = &channels;
        nodeConfig.pOutputChannels = &channels;

        ma_result result = ma_node_init(ma_engine_get_node_graph(engine), &nodeConfig, nullptr, &bus->base);
        if (result != MA_SUCCESS) {
            DEBUG_ERROR(std::string("Couldn't create the reverb bus: ") + ma_result_description(result));
            delete bus;
            return;
        }

        ma_node_attach_output_bus(&bus->base, 0, ma_engine_get_endpoint(engine), 0);
        ma_node_set_output_bus_volume(&bus->base, 0, 0.0f);

        reverbBus = bus;
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

        // Direct sound to the output, a send of it to the reverb bus : two outputs of a splitter, each with its own volume
        if (reverbBus) {
            ma_splitter_node_config splitterConfig = ma_splitter_node_config_init(ma_engine_get_channels(engine));
            ma_splitter_node* splitter = new ma_splitter_node();
            if (ma_splitter_node_init(ma_engine_get_node_graph(engine), &splitterConfig, nullptr, splitter) == MA_SUCCESS) {
                ma_node_attach_output_bus(sound->sound, 0, splitter, 0);
                ma_node_attach_output_bus(splitter, 0, ma_engine_get_endpoint(engine), 0);
                ma_node_attach_output_bus(splitter, 1, &reverbBus->base, 0);
                ma_node_set_output_bus_volume(splitter, 1, 0.0f);
                sound->splitter = splitter;
            }
            else {
                delete splitter;
            }
        }

        Core::GetEngine().GetAudioIDManager()->AssignID(id, sound);
        soundCount++;
    }

    static void DestroySound(Sound* sound)
    {
        ma_sound_uninit(sound->sound);
        if (sound->splitter) {
            ma_splitter_node_uninit(static_cast<ma_splitter_node*>(sound->splitter), nullptr);
            delete static_cast<ma_splitter_node*>(sound->splitter);
        }
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

    bool AudioManager::PlayOneShot(const std::string& pathInProject, glm::vec3 pos, float volume, bool spatialize)
    {
        Audio::AudioIDManager* ids = Core::GetEngine().GetAudioIDManager();
        const AudioID id = ids->GenerateNewID();
        CreateSound(id, pathInProject, pos, spatialize);
        if (!ids->GetSoundFromID(id))
            return false;

        PlaySound(id, volume);
        SoundParams params;
        params.worldPosition = glm::dvec3(pos);
        UpdateSound(id, pos, volume, params);       // after PlaySound, which scales the volume for a source that is updated every tick
        oneShots.push_back(id);
        return true;
    }

    void AudioManager::PauseSound(AudioID id)
    {
        Sound* sound = Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id);
        if(sound && sound->isPlaying){
            ma_sound_stop(sound->sound);
            sound->isPlaying = false;
        }
    }

    void AudioManager::UpdateSound(AudioID id, glm::vec3 pos, float volume, const SoundParams& params)
    {
        Sound* sound = Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id);
        if(!sound)
            return;

        sound->pos = pos;
        sound->initialVolume = volume;
        sound->worldPos = params.worldPosition;
        sound->dopplerLevel = params.dopplerLevel;
        sound->reverbSend = std::clamp(params.reverbSend, 0.0f, 1.0f);
    }

    void AudioManager::SetSpatialize(AudioID id, bool spatialize)
    {
        Sound* sound = Core::GetEngine().GetAudioIDManager()->GetSoundFromID(id);
        if(sound)
            sound->spatialize = spatialize;
    }

    void AudioManager::Update(const ListenerState& listener, float maxDistance)
    {
        if(maxDistance <= 0){
            DEBUG_FATAL("maxDistance can't be >= 0");
        }

        // The velocities are smoothed : a source moved by the fixed steps of the simulation doesn't change place on every frame
        constexpr float kVelocitySmoothing = 0.08f;
        constexpr float kPitchSmoothing = 0.04f;
        const float velocityBlend = 1.0f - std::exp(-tickDelta / kVelocitySmoothing);
        const float pitchBlend = 1.0f - std::exp(-tickDelta / kPitchSmoothing);

        for (const auto& pair : Core::GetEngine().GetAudioIDManager()->GetAudioMap()) {
            Sound* sound = pair.second;

            // Where it is, even when it isn't playing : so the first frame after it starts has a velocity to start from
            if(sound->velocityTick != tickIndex){
                sound->velocityTick = tickIndex;
                if(sound->hasPrevPos && tickDelta > 1e-4f){
                    glm::dvec3 measured = (sound->worldPos - sound->prevWorldPos) / static_cast<double>(tickDelta);
                    // A jump (a teleport, a world that loads in) is not a movement
                    if(glm::length(measured) > static_cast<double>(listener.speedOfSound) * 2.0)
                        measured = glm::dvec3(0.0);
                    sound->velocity += (measured - sound->velocity) * static_cast<double>(velocityBlend);
                }
                else{
                    sound->velocity = glm::dvec3(0.0);
                }
                sound->prevWorldPos = sound->worldPos;
                sound->hasPrevPos = true;
            }

            if(!sound->isPlaying)
                continue;

            // Reached the end on its own (the audio thread never touches our state, it is polled here)
            if(ma_sound_at_end(sound->sound)){
                sound->isPlaying = false;
                continue;
            }

            // Doppler : the pitch follows the speed at which the source and the listener come closer or move apart
            if(sound->spatialize){
                const float target = ComputeDopplerPitch(listener.worldPosition, listener.velocity, sound->worldPos, sound->velocity,
                                                         listener.speedOfSound, listener.dopplerFactor * sound->dopplerLevel);
                sound->pitch += (target - sound->pitch) * pitchBlend;
            }
            else{
                sound->pitch = 1.0f;
            }
            ma_sound_set_pitch(sound->sound, sound->pitch);

            if(sound->spatialize){
                float pan = glm::sin(glm::orientedAngle(glm::normalize(glm::vec2(sound->pos.x - listener.position.x, sound->pos.z - listener.position.z)), listener.facing));
                if (pan < -1.0f) pan = -1.0f;
                if (pan > 1.0f) pan = 1.0f;
                float volume = 1.0f - (glm::distance(listener.position, sound->pos) / maxDistance);
                volume *= masterVolume/100;
                if (volume < 0.0f) volume = 0.0f;
                if (volume > 1.0f) volume = 1.0f;

                volume *= sound->initialVolume;

                ma_sound_set_pan(sound->sound, -1 * pan);
                ma_sound_set_volume(sound->sound, volume);
            }
            else{
                // Ambient/music sources skip positional pan+attenuation entirely - flat volume,
                // native stereo image left untouched (never panned away from center).
                float volume = masterVolume/100 * sound->initialVolume;
                if (volume < 0.0f) volume = 0.0f;
                if (volume > 1.0f) volume = 1.0f;

                ma_sound_set_volume(sound->sound, volume);
            }
        }
    }

    void AudioManager::SetReverb(const ReverbMix& mix)
    {
        reverbTarget = mix;
        ApplyReverb();
    }

    void AudioManager::ApplyReverb()
    {
        if (!reverbBus)
            return;

        // The bus moves toward the zone the listener is in. Out of every zone the settings are kept while the level fades out, so the
        // tail dies with the acoustics it was made in rather than sliding to another room.
        const float coverageBlend = 1.0f - std::exp(-tickDelta / 0.15f);
        const float paramsBlend = 1.0f - std::exp(-tickDelta / 0.30f);

        if (reverbTarget.coverage > 0.0f) {
            // Next to nothing is heard yet : the settings can jump to the ones of the zone
            const float t = reverbCoverage < 0.01f ? 1.0f : paramsBlend;
            reverbCurrent = LerpReverb(reverbCurrent, reverbTarget.params, t);
        }
        reverbCoverage += (reverbTarget.coverage - reverbCoverage) * coverageBlend;
        if (reverbCoverage < 1e-4f)
            reverbCoverage = 0.0f;

        reverbBus->roomSize.store(std::clamp(reverbCurrent.roomSize, 0.0f, 1.0f));
        reverbBus->damping.store(std::clamp(reverbCurrent.damping, 0.0f, 1.0f));
        reverbBus->width.store(std::clamp(reverbCurrent.width, 0.0f, 1.0f));
        reverbBus->preDelayMs.store(std::clamp(reverbCurrent.preDelayMs, 0.0f, kMaxPreDelayMs));
        reverbBus->lowCutHz.store(reverbCurrent.lowCutHz);
        reverbBus->highCutHz.store(reverbCurrent.highCutHz);

        const float wet = std::clamp(reverbCurrent.wetLevel, 0.0f, 2.0f) * reverbCoverage * (masterVolume / 100.0f);
        ma_node_set_output_bus_volume(&reverbBus->base, 0, wet);

        // What each sound sends to the bus, and how much of its direct sound the room takes away
        const float dryLoss = (1.0f - std::clamp(reverbCurrent.dryLevel, 0.0f, 1.0f)) * reverbCoverage;
        for (const auto& pair : Core::GetEngine().GetAudioIDManager()->GetAudioMap()) {
            Sound* sound = pair.second;
            if (!sound->splitter)
                continue;

            ma_node_set_output_bus_volume(sound->splitter, 1, sound->reverbSend);
            ma_node_set_output_bus_volume(sound->splitter, 0, 1.0f - sound->reverbSend * dryLoss);
        }
    }

    void AudioManager::Shutdown()
    {
        for (const auto& pair : Core::GetEngine().GetAudioIDManager()->GetAudioMap()) {
            DestroySound(pair.second);
            Core::GetEngine().GetAudioIDManager()->DestroyID(pair.first);
        }
        soundCount = 0;
        oneShots.clear();

        DestroyReverbBus(reverbBus);
        reverbBus = nullptr;

        if (engine) {
            ma_engine_uninit(engine);
            delete engine;
            engine = nullptr;
        }
    }

    void AudioManager::Tick()
    {
        // Real time between two ticks : the doppler and the fades don't follow the time scale of the game
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        tickDelta = lastTickTime < 0.0 ? 0.0f : static_cast<float>(std::clamp(now - lastTickTime, 0.0, 0.25));
        lastTickTime = now;
        ++tickIndex;

        // The one shot sounds that ended
        for (size_t i = 0; i < oneShots.size();) {
            Sound* sound = Core::GetEngine().GetAudioIDManager()->GetSoundFromID(oneShots[i]);
            if (!sound || (sound->sound && ma_sound_at_end(sound->sound))) {
                RemoveSound(oneShots[i]);
                oneShots.erase(oneShots.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }

        std::vector<ReverbZoneSample> zones;

        // (Was `if(int worldCount = Count() > 0)` : the comparison bound first, so worldCount was 0 or 1 and only world 0 was heard)
        const int worldCount = Core::GetEngine().GetWorldManager()->GetLoadedWorldCount();
        {
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
                    continue;

                ListenerState state;
                state.position = listener->parent->transform->GetWorldPosition();
                const glm::vec3 forward = listener->parent->transform->GetWorldForward();
                state.facing = glm::normalize(glm::vec2(forward.x, forward.z));
                state.worldPosition = listener->parent->transform->GetWorldPositionD();
                state.dopplerFactor = listener->dopplerFactor;
                state.speedOfSound = listener->speedOfSound;

                auto previous = previousListenerPositions.find(i);
                if(previous != previousListenerPositions.end() && tickDelta > 1e-4f){
                    glm::dvec3 measured = (state.worldPosition - previous->second) / static_cast<double>(tickDelta);
                    if(glm::length(measured) > static_cast<double>(state.speedOfSound) * 2.0)
                        measured = glm::dvec3(0.0);
                    // Smoothed like the sources are
                    const double blend = 1.0 - std::exp(-static_cast<double>(tickDelta) / 0.08);
                    listenerVelocities[i] += (measured - listenerVelocities[i]) * blend;
                }
                else{
                    listenerVelocities[i] = glm::dvec3(0.0);
                }
                previousListenerPositions[i] = state.worldPosition;
                state.velocity = listenerVelocities[i];

                for(auto& [zoneId, zone] : Core::GetEngine().GetWorldManager()->GetWorldAt(i)->Ext<AudioWorldData>().reverbZones){
                    if(!zone->Active())
                        continue;

                    const float weight = zone->SampleWeight(state.worldPosition);
                    if(weight > 0.0f)
                        zones.push_back({ weight, zone->priority, zone->GetParams() });
                }

                AudioManager::Update(state, 100.0f);
            }
        }

        SetReverb(MixReverbZones(std::move(zones)));
    }
}
