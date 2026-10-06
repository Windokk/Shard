#include "world_extension.hpp"

namespace Shard::Engine::Worlds{

    namespace {

        std::vector<std::pair<std::type_index, WorldExtensionFactory>>& Factories()
        {
            static std::vector<std::pair<std::type_index, WorldExtensionFactory>> factories;
            return factories;
        }

        std::vector<WorldSettingsAssetRefs>& SettingsRefs()
        {
            static std::vector<WorldSettingsAssetRefs> refs;
            return refs;
        }
    }

    void RegisterWorldExtension(std::type_index type, WorldExtensionFactory factory)
    {
        for(auto& [registered, f] : Factories()){
            if(registered == type)
                return;
        }
        Factories().emplace_back(type, std::move(factory));
    }

    const std::vector<std::pair<std::type_index, WorldExtensionFactory>>& GetWorldExtensionFactories()
    {
        return Factories();
    }

    void RegisterWorldSettingsAssetRefs(WorldSettingsAssetRefs refs)
    {
        SettingsRefs().push_back(std::move(refs));
    }

    const std::vector<WorldSettingsAssetRefs>& GetWorldSettingsAssetRefs()
    {
        return SettingsRefs();
    }
}
