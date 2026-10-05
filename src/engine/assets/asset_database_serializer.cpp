#include "asset_database_serializer.hpp"

#include "engine/debugging/logger.hpp"
#include "engine/core/engine.hpp"

using namespace nlohmann;

namespace Shard::Engine::Serialization{

    void DeserializeAssetDataBase(const Filesystem::Path resourcesPath, const Filesystem::Path databasePath){

        if(!databasePath.Exists()){
            DEBUG_ERROR("Database at path : \"" + databasePath.full +"\" doesn't exist !");
            return; 
        }

        if(!resourcesPath.Exists() || !resourcesPath.IsDirectory()){
            DEBUG_ERROR("Resources root at path : \"" + resourcesPath.full +"\" doesn't exist or is not a directory !");
            return;
        }

        std::string src = databasePath.ReadFile();

        try {
            json data = json::parse(src);

            int maxID = 0;

            for(auto [key, value] : data.items()){
                std::shared_ptr<Filesystem::AssetInfos> assetInfos = std::make_shared<Filesystem::AssetInfos>();
                std::vector<Filesystem::AssetID> dependencies;
                assetInfos->baseInfos = Core::GetEngine().GetFileManager()->GetFileInfos(resourcesPath / key);
                if (value.is_object() && value.size() > 1 && value.contains("dependencies")) {
                    for (const auto& dependencyID : value["dependencies"]) {
                        if (dependencyID.is_number_integer()) {
                            dependencies.push_back(Filesystem::AssetID(dependencyID.get<int>()));
                        }
                    }
                }
                assetInfos->dependencies = dependencies;

                if (value.contains("id")) {
                    int currentID = value["id"].get<int>();
                    maxID = std::max(maxID, currentID);
                    Core::GetEngine().GetAssetIDManager()->AssignID(Core::GetEngine().GetAssetIDManager()->GenerateNewIDWithValue(currentID), assetInfos);
                }
            }

            Core::GetEngine().GetAssetIDManager()->InitCounter(maxID+1);

        } catch (const json::parse_error& e) {
            DEBUG_ERROR("JSON parse error for : ", databasePath.full , ", error : " , (std::string)e.what());
            return;
        }
    }

    void SerializeAssetDataBase(const Filesystem::Path databasePath)
    {
        ordered_json fileContent;

        for(auto& pair : Core::GetEngine().GetAssetIDManager()->AssetIDMap){

            if(pair.first.GetAsInt() > 500) //This means it's a project resource, as engine resources IDs stop at 500
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