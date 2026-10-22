#include <cmath>

#include <shard/shard_audio.h>

#include "engine/audio/audio_manager.hpp"
#include "engine/audio/audio_source.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"

// shard.audio : the AudioSource of an actor, and one shot sounds. Lives in the engine root, not in src/engine/plugins/abi with the other
// interfaces: the plugin layer is below audio and may not include it (scripts/check_deps.py), and c_internal.hpp declares AudioApi.

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        using Objects::Components::AudioSource;

        Audio::AudioManager* Manager()
        {
            Core::IEngineContext* engine = Core::gSharedEnginePtr;
            return engine ? engine->GetAudioManager() : nullptr;
        }

        ShardResult SourceOf(ShardActor handle, std::shared_ptr<AudioSource>& out)
        {
            if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
            std::shared_ptr<Objects::Actor> actor = ActorOf(handle);
            if (!actor) return SHARD_E_STALE_HANDLE;
            if (!Manager()) return SHARD_E_UNSUPPORTED;
            const auto sources = actor->GetComponents<AudioSource>();
            if (sources.empty()) return SHARD_E_NOT_FOUND;
            out = sources.front();
            return SHARD_OK;
        }

        ShardResult SHARD_CALL Play(ShardActor actor)
        {
            try {
                std::shared_ptr<AudioSource> source;
                const ShardResult r = SourceOf(actor, source);
                if (r != SHARD_OK) return r;
                source->Play();
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Pause(ShardActor actor)
        {
            try {
                std::shared_ptr<AudioSource> source;
                const ShardResult r = SourceOf(actor, source);
                if (r != SHARD_OK) return r;
                source->Pause();
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetVolume(ShardActor actor, float volume)
        {
            try {
                if (!std::isfinite(volume) || volume < 0.0f) return SHARD_E_INVALID_ARG;
                std::shared_ptr<AudioSource> source;
                const ShardResult r = SourceOf(actor, source);
                if (r != SHARD_OK) return r;
                source->SetVolume(volume);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL PlayOneShot(ShardStr clip, const float position[3], float volume, int32_t spatial)
        {
            try {
                if (!position || !std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2])) return SHARD_E_INVALID_ARG;
                if (!std::isfinite(volume) || volume < 0.0f) return SHARD_E_INVALID_ARG;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                Core::IEngineContext* engine = Core::gSharedEnginePtr;
                Audio::AudioManager* audio = engine ? engine->GetAudioManager() : nullptr;
                Filesystem::AssetIDManager* assets = engine ? engine->GetAssetIDManager() : nullptr;
                if (!audio || !assets) return SHARD_E_UNSUPPORTED;
                const std::string path = ToString(clip);
                if (!assets->GetAssetFromID(assets->GetIDFromNameInProject(path))) return SHARD_E_NOT_FOUND;
                audio->PlayOneShot(path, glm::vec3(position[0], position[1], position[2]), volume, spatial != 0);     // a silent host plays nothing
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    const void* AudioApi()
    {
        static const ShardAudioApi api = { sizeof(ShardAudioApi), &Play, &Pause, &SetVolume, &PlayOneShot };
        return &api;
    }
}
