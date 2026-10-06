#include "material_asset_refs.hpp"

#include <nlohmann/json.hpp>

#include "engine/core/diagnostics/logger.hpp"

using namespace nlohmann;

namespace Shard::Engine::Serialization{

    MaterialAssetRefs PeekMaterialAssetRefs(const Filesystem::Path &path)
    {
        MaterialAssetRefs refs;

        if(!path.Exists()){
            return refs;
        }

        std::string src = path.ReadFile();

        try {
            json data = json::parse(src);

            if(data.contains("shader") && data["shader"].is_string()){
                refs.shaderPathInProject = data["shader"].get<std::string>();
            }

            if(data.contains("uniforms")){
                for(auto& uniform : data["uniforms"]){
                    for(auto it = uniform.begin(); it != uniform.end(); ++it){
                        if(it.value().is_string()){
                            refs.texturePathsInProject.push_back(it.value().get<std::string>());
                        }
                    }
                }
            }

            refs.success = true;

        } catch (const json::parse_error& e) {
            DEBUG_ERROR("JSON parse error: " + (std::string)e.what());
        }

        return refs;
    }
}
