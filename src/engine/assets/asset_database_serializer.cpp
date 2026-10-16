#include "asset_database_serializer.hpp"

#include "engine/core/diagnostics/logger.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using namespace nlohmann;

namespace Shard::Engine::Serialization{

    void DeserializeAssetDataBase(const Filesystem::Path resourcesPath, const Filesystem::Path databasePath,
                                  Filesystem::FileManager& files, Filesystem::AssetIDManager& ids, int firstID){

        // Whatever happens below, the IDs made from now on start at firstID at least
        auto initCounter = [&](int next){ ids.InitCounter(std::max({next, firstID, ids.NextID()})); };

        if(!databasePath.Exists()){
            DEBUG_ERROR("Database at path : \"" + databasePath.full +"\" doesn't exist !");
            initCounter(0);
            return;
        }

        if(!resourcesPath.Exists() || !resourcesPath.IsDirectory()){
            DEBUG_ERROR("Resources root at path : \"" + resourcesPath.full +"\" doesn't exist or is not a directory !");
            initCounter(0);
            return;
        }

        std::string src = databasePath.ReadFile();

        try {
            json data = json::parse(src);

            struct Entry { int id; std::shared_ptr<Filesystem::AssetInfos> infos; std::vector<int> dependencies; };
            std::vector<Entry> entries;

            int maxID = 0;

            for(auto [key, value] : data.items()){
                if (!value.contains("id"))
                    continue;

                Entry& entry = entries.emplace_back();
                entry.id = value["id"].get<int>();
                entry.infos = std::make_shared<Filesystem::AssetInfos>();
                entry.infos->baseInfos = files.GetFileInfos(resourcesPath / key);
                if (value.is_object() && value.size() > 1 && value.contains("dependencies")) {
                    for (const auto& dependencyID : value["dependencies"]) {
                        if (dependencyID.is_number_integer()) {
                            entry.dependencies.push_back(dependencyID.get<int>());
                        }
                    }
                }

                if (entry.id >= firstID && !ids.GetAssetFromID(Filesystem::AssetID(entry.id)))
                    maxID = std::max(maxID, entry.id);
            }

            // An ID out of this database's range (a project made before the range was enforced started at 1) would overwrite an
            // engine resource : moved past the others. Its references in the worlds are by name, only the dependencies are by ID.
            std::unordered_map<int, int> remapped;
            std::unordered_set<int> kept;
            int nextFree = std::max({maxID + 1, firstID, ids.NextID()});

            for (Entry& entry : entries) {
                if (entry.id >= firstID && !ids.GetAssetFromID(Filesystem::AssetID(entry.id)) && kept.insert(entry.id).second)
                    continue;

                DEBUG_WARNING("Asset database \"" + databasePath.full + "\" : \"" + entry.infos->baseInfos.nameInProject + "\" had the ID "
                              + std::to_string(entry.id) + ", reserved or already taken. Now " + std::to_string(nextFree) + ".");
                remapped[entry.id] = nextFree;
                entry.id = nextFree++;
            }

            for (Entry& entry : entries) {
                for (int dependency : entry.dependencies) {
                    auto it = remapped.find(dependency);
                    entry.infos->dependencies.push_back(Filesystem::AssetID(it != remapped.end() ? it->second : dependency));
                }
                ids.AssignID(ids.GenerateNewIDWithValue(entry.id), entry.infos);
            }

            initCounter(nextFree);

        } catch (const json::parse_error& e) {
            DEBUG_ERROR("JSON parse error for : ", databasePath.full , ", error : " , (std::string)e.what());
            initCounter(0);
            return;
        }
    }

    void SerializeAssetDataBase(const Filesystem::Path databasePath, const Filesystem::AssetIDManager& ids)
    {
        ordered_json fileContent;

        for(auto& pair : ids.AssetIDMap){

            if(pair.first.GetAsInt() >= Filesystem::kFirstProjectAssetID) // A project resource : the engine's stop below
            {
                ordered_json assetData;
                assetData["id"] = pair.first.GetAsInt();
                for(auto& dependency : pair.second->dependencies){
                    assetData["dependencies"].push_back(dependency.GetAsInt());
                }
                fileContent[pair.second->baseInfos.nameInProject] = assetData;
            }
        }

        databasePath.WriteFile(fileContent.dump());
    }
}
