#include "audio_world_data.hpp"

#include "engine/audio/audio_source.hpp"
#include "engine/world/components/registry/component_registry.hpp"

namespace Shard::Engine::Audio{

    using Objects::Components::AudioSource;

    void AudioWorldData::OnComponentAdded(Worlds::World&, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component, bool cloned)
    {
        if(auto source = std::dynamic_pointer_cast<AudioSource>(component)){
            sources.emplace(idInWorld, source);

            if(cloned)
                source->Update();
        }
    }

    void AudioWorldData::OnComponentRemoved(Worlds::World&, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component)
    {
        if(component->IsInstanceOf<AudioSource>())
            sources.erase(idInWorld);
    }

    void AudioWorldData::OnPlay(Worlds::World&)
    {
        for(auto& [id, audio] : sources){
            if(audio->Active()){
                audio->OnPlay();
            }
        }
    }

    void RegisterAudioModule()
    {
        Objects::Components::GetComponentRegistry().RegisterBuiltinComponent<AudioSource>("audio",
            [](const nlohmann::json& component, std::vector<Core::Resources::ResourceKey>& out){
                if (component.contains("sound") && component["sound"].is_string() && !component["sound"].get<std::string>().empty())
                    out.push_back({Core::Resources::AssetKind::Sound, component["sound"].get<std::string>()});
            });
        Worlds::RegisterWorldExtension<AudioWorldData>();
    }
}
