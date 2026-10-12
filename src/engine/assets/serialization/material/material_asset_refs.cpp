#include "material_asset_refs.hpp"

#include <cctype>

#include <nlohmann/json.hpp>

#include "engine/core/diagnostics/logger.hpp"

using namespace nlohmann;

namespace Shard::Engine::Serialization{

    bool IsColorTextureSlot(const std::string& samplerName)
    {
        std::string lower;
        lower.reserve(samplerName.size());
        for (char c : samplerName)
            lower.push_back(char(std::tolower(static_cast<unsigned char>(c))));

        // The naming the default textures of a material follow as well (see GLMaterial::GetDefaultTexture)
        for (const char* prefix : { "albedo", "diffuse", "emissive", "basecolor" })
            if (lower.rfind(prefix, 0) == 0)
                return true;
        return false;
    }

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
                            refs.textures.push_back({it.key(), it.value().get<std::string>()});
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
