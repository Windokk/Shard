#include "render_asset_kinds.hpp"

#include "engine/assets/resources_manager.hpp"
#include "engine/assets/serialization/material/material_asset_refs.hpp"
#include "engine/core/diagnostics/logger.hpp"

#include "engine/renderer/material/material_serializer.hpp"

#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/envmap.hpp"
#include "engine/renderer/features/lighting/probe_bake.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/material/shader.hpp"
#include "engine/renderer/material/compute_shader.hpp"
#include "engine/renderer/material/material.hpp"

namespace Shard::Engine::Rendering{

    using namespace Core::Resources;

    namespace {

        std::shared_ptr<Mesh> LoadModel(const Filesystem::Path &path)
        {
            ufbx_load_opts opts = { 0 }; // Optional, pass NULL for defaults
            ufbx_error error; // Optional, pass NULL if you don't care about errors
            const std::string filePath = path.GetNativePath();
            ufbx_scene *scene = ufbx_load_file(filePath.c_str(), &opts, &error);
            if (!scene) {
                DEBUG_ERROR(
                    "Failed to load " + path.full + " : " +
                    (error.description.data ? error.description.data : "Unknown error"));
                return nullptr;
            }

            if (scene->meshes.count > 1) {
                ufbx_free_scene(scene);
                DEBUG_ERROR("Multiple meshes per fbx file isn't supported yet.");
                return nullptr;
            }

            ufbx_mesh* ufbx_mesh = scene->meshes.data[0];

            ufbx_node* mesh_node = nullptr;

            for (size_t i = 0; i < scene->nodes.count; i++) {
                ufbx_node* node = scene->nodes.data[i];
                if (node->mesh == ufbx_mesh) {
                    mesh_node = node;
                    break;
                }
            }

            std::shared_ptr<Mesh> mesh = Mesh::Create();
            mesh->CreateFromFBX(ufbx_mesh, scene->settings.unit_meters, scene->materials, mesh_node);

            ufbx_free_scene(scene);

            return mesh;
        }

        std::shared_ptr<Texture2D> LoadTexture(const Filesystem::Path &path)
        {
            TextureSpecifications specs = {};
            // Trilinear : mips are already generated (specs.generateMips defaults true) but the default
            // Linear min filter never samples them, so every material texture was minified straight from
            // level 0 - aliasing/shimmer on detailed textures at distance. GLTexture2D adds anisotropy on
            // top when mips are present.
            specs.minFilter = TextureFilter::LinearMipmapLinear;
            return Texture2D::Create(specs, path);
        }

        std::shared_ptr<EnvironmentMap> LoadEnvMap(const Filesystem::Path &path)
        {
            TextureSpecifications specs = {};
            std::shared_ptr<EnvironmentMap> envMap = EnvironmentMap::Create(specs, path);
            if(!envMap){
                DEBUG_ERROR("Error during cubemap import");
                return nullptr;
            }
            return envMap;
        }

        std::shared_ptr<Shader> LoadShader(const Filesystem::AssetInfos &infos)
        {
            // The database entry is the .vert ; the other stages sit next to it
            Filesystem::Path vertPath = Filesystem::Path(infos.baseInfos.path.GetParent()) / (infos.baseInfos.name + ".vert");
            Filesystem::Path fragPath = Filesystem::Path(infos.baseInfos.path.GetParent()) / (infos.baseInfos.name + ".frag");
            Filesystem::Path geomPath = Filesystem::Path(infos.baseInfos.path.GetParent()) / (infos.baseInfos.name + ".geom");
            return Shader::Create(vertPath, fragPath, geomPath.Exists() ? geomPath : Filesystem::Path(""));
        }

        std::shared_ptr<Material> LoadMaterial(const Filesystem::Path &path)
        {
            std::shared_ptr<Material> mat = Serialization::DeserializeMaterial(path);
            if(!mat){
                DEBUG_ERROR("Error during material import.");
                return nullptr;
            }
            return mat;
        }

        // Every resource type the cache hands an AssetID to has the same setter.
        template<class T>
        std::function<void(void*, Filesystem::AssetID)> AssetIDSetter()
        {
            return [](void* resource, Filesystem::AssetID id){ static_cast<T*>(resource)->SetAssetID(id); };
        }
    }

    void RegisterAssetKinds(ResourcesManager &resources)
    {
        {
            AssetKindInfo info;
            info.load = [](const std::string&, const Filesystem::AssetInfos& infos) -> std::shared_ptr<void> { return LoadModel(infos.baseInfos.path); };
            info.setAssetID = AssetIDSetter<Mesh>();
            // The file is decoded on a worker thread, the GPU upload happens on the main thread
            info.prefetch = [](ResourcesManager& resources, const std::string& pathInProject, const Filesystem::Path& file) -> std::function<void()> {
                auto data = std::make_shared<MeshCPUData>(DecodeMeshFile(file));
                if(!data->success)
                    return nullptr;

                return [&resources, pathInProject, data](){
                    std::shared_ptr<Mesh> mesh = Mesh::Create();
                    mesh->CreateFromData(*data);
                    resources.Adopt(AssetKind::Mesh, pathInProject, mesh);
                };
            };
            resources.RegisterKind(AssetKind::Mesh, std::move(info));
        }

        {
            AssetKindInfo info;
            info.load = [](const std::string&, const Filesystem::AssetInfos& infos) -> std::shared_ptr<void> { return LoadTexture(infos.baseInfos.path); };
            info.setAssetID = AssetIDSetter<Texture2D>();
            info.prefetch = [](ResourcesManager& resources, const std::string& pathInProject, const Filesystem::Path& file) -> std::function<void()> {
                auto data = std::make_shared<TextureDecodeResult>(DecodeTextureFile(file));
                if(!data->success)
                    return nullptr;

                return [&resources, pathInProject, data](){
                    TextureSpecifications specs;
                    specs.internalFormat = data->format;
                    specs.width = data->width;
                    specs.height = data->height;
                    // Trilinear + (in GLTexture2D) anisotropy - mips are generated by default but the
                    // default Linear min filter never uses them, so textures minified straight from world
                    // 0 and shimmered at distance/grazing angles. Match LoadTexture.
                    specs.minFilter = TextureFilter::LinearMipmapLinear;

                    std::shared_ptr<Texture2D> texture = Texture2D::Create(specs, data->pixels.data());
                    resources.Adopt(AssetKind::Texture, pathInProject, texture);
                };
            };
            resources.RegisterKind(AssetKind::Texture, std::move(info));
        }

        {
            AssetKindInfo info;
            info.load = [](const std::string&, const Filesystem::AssetInfos& infos) -> std::shared_ptr<void> { return LoadEnvMap(infos.baseInfos.path); };
            info.setAssetID = AssetIDSetter<EnvironmentMap>();
            resources.RegisterKind(AssetKind::EnvMap, std::move(info));
        }

        {
            AssetKindInfo info;
            info.load = [](const std::string&, const Filesystem::AssetInfos& infos) -> std::shared_ptr<void> { return LoadShader(infos); };
            info.setAssetID = AssetIDSetter<Shader>();
            info.nameSuffix = ".vert";
            info.dependencySuffixes = {".vert", ".frag", ".geom"};
            resources.RegisterKind(AssetKind::Shader, std::move(info));
        }

        {
            AssetKindInfo info;
            info.load = [](const std::string&, const Filesystem::AssetInfos& infos) -> std::shared_ptr<void> { return ComputeShader::Create(infos.baseInfos.path); };
            info.setAssetID = AssetIDSetter<ComputeShader>();
            info.nameSuffix = ".comp";
            resources.RegisterKind(AssetKind::ComputeShader, std::move(info));
        }

        {
            AssetKindInfo info;
            info.load = [](const std::string&, const Filesystem::AssetInfos& infos) -> std::shared_ptr<void> { return LoadMaterial(infos.baseInfos.path); };
            info.setAssetID = AssetIDSetter<Material>();
            info.ownerType = Filesystem::Type::T_MATERIAL;
            info.dependencies = [](const Filesystem::Path& path){
                std::vector<ResourceKey> keys;
                Serialization::MaterialAssetRefs refs = Serialization::PeekMaterialAssetRefs(path);
                if(!refs.shaderPathInProject.empty())
                    keys.push_back({AssetKind::Shader, refs.shaderPathInProject});
                for(auto& p : refs.texturePathsInProject)
                    keys.push_back({AssetKind::Texture, p});
                return keys;
            };
            resources.RegisterKind(AssetKind::Material, std::move(info));
        }

        {
            // CPU-side data only, not an asset object : no AssetID. Normally decoded by the world
            // prefetcher on a worker thread and adopted, this is the fallback.
            AssetKindInfo info;
            info.load = [](const std::string& pathInProject, const Filesystem::AssetInfos& infos) -> std::shared_ptr<void> {
                if(infos.baseInfos.nameInProject != pathInProject){
                    DEBUG_ERROR("Unknown probe bake : " + pathInProject);
                    return nullptr;
                }
                return DecodeProbeBakeFile(infos.baseInfos.path);
            };
            // No GL work to do on the main thread : the decoded CPU data just goes into the cache, and
            // ProbeVolume::Activate() uploads it when the world's volumes come up.
            info.prefetch = [](ResourcesManager& resources, const std::string& pathInProject, const Filesystem::Path& file) -> std::function<void()> {
                std::shared_ptr<ProbeBakeData> data = DecodeProbeBakeFile(file);
                if(!data)
                    return nullptr;

                return [&resources, pathInProject, data](){ resources.Adopt(AssetKind::ProbeBake, pathInProject, data); };
            };
            info.unknownMessage = "Unknown probe bake : ";
            resources.RegisterKind(AssetKind::ProbeBake, std::move(info));
        }
    }

}
