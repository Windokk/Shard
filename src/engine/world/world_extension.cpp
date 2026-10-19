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

    bool UnregisterWorldExtension(std::type_index type)
    {
        auto& factories = Factories();
        for(auto it = factories.begin(); it != factories.end(); ++it){
            if(it->first == type){
                factories.erase(it);
                return true;
            }
        }
        return false;
    }

    const std::vector<std::pair<std::type_index, WorldExtensionFactory>>& GetWorldExtensionFactories()
    {
        return Factories();
    }

    void RegisterWorldSettingsAssetRefs(WorldSettingsAssetRefs refs)
    {
        SettingsRefs().push_back(std::move(refs));
    }

    namespace {
        std::vector<uint32_t>& SettingsRefIds()
        {
            static std::vector<uint32_t> ids;
            return ids;
        }
        uint32_t gNextSettingsRefId = 0;
    }

    uint32_t RegisterWorldSettingsAssetRefsWithId(WorldSettingsAssetRefs refs)
    {
        // Ids run parallel to SettingsRefs() ; entries registered without an id carry 0
        while(SettingsRefIds().size() < SettingsRefs().size())
            SettingsRefIds().push_back(0);
        const uint32_t id = ++gNextSettingsRefId;
        SettingsRefs().push_back(std::move(refs));
        SettingsRefIds().push_back(id);
        return id;
    }

    bool UnregisterWorldSettingsAssetRefs(uint32_t id)
    {
        if(id == 0) return false;
        auto& ids = SettingsRefIds();
        for(size_t i = 0; i < ids.size() && i < SettingsRefs().size(); ++i){
            if(ids[i] == id){
                // The slot stays (callers hold the vector by reference while they iterate) but does nothing any more
                SettingsRefs()[i] = [](const nlohmann::json&, std::vector<Core::Resources::ResourceKey>&) {};
                ids[i] = 0;
                return true;
            }
        }
        return false;
    }

    namespace {
        std::vector<std::pair<uint32_t, WorldJsonFilter>>& JsonFilters()
        {
            static std::vector<std::pair<uint32_t, WorldJsonFilter>> filters;
            return filters;
        }
        uint32_t gNextFilterId = 0;
    }

    uint32_t AddWorldJsonFilter(WorldJsonFilter filter)
    {
        const uint32_t id = ++gNextFilterId;
        JsonFilters().emplace_back(id, std::move(filter));
        return id;
    }

    bool RemoveWorldJsonFilter(uint32_t id)
    {
        auto& filters = JsonFilters();
        for(auto it = filters.begin(); it != filters.end(); ++it){
            if(it->first == id){
                filters.erase(it);
                return true;
            }
        }
        return false;
    }

    void ApplyWorldJsonFilters(const std::string& pathInProject, nlohmann::json& world)
    {
        // A copy of the list : a filter may add or remove filters
        const auto filters = JsonFilters();
        for(const auto& [id, filter] : filters)
            filter(pathInProject, world);
    }

    namespace {
        WorldSaveGuard& SaveGuard()
        {
            static WorldSaveGuard guard;
            return guard;
        }
    }

    void SetWorldSaveGuard(WorldSaveGuard guard) { SaveGuard() = std::move(guard); }

    bool AllowWorldSave(const std::string& pathInProject)
    {
        return !SaveGuard() || SaveGuard()(pathInProject);
    }

    const std::vector<WorldSettingsAssetRefs>& GetWorldSettingsAssetRefs()
    {
        return SettingsRefs();
    }
}
