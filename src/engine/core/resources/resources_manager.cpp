#include "resources_manager.hpp"

#include "engine/serialization/material/material_serializer.hpp"

#include "engine/serialization/assets/asset_database_serializer.hpp"

#include "engine/core/engine.hpp"

#include "engine/projects/project.hpp"

#include "engine/rendering/texture/cubemap/envmap.hpp"

#include "engine/rendering/lighting/probe_bake.hpp"

#include "engine/rendering/mesh/mesh.hpp"
#include "engine/rendering/pipeline/pipeline.hpp"
#include "engine/rendering/shader/shader.hpp"
#include "engine/rendering/shader/compute_shader.hpp"
#include "engine/rendering/material/material.hpp"

#include "engine/audio/sound_asset.hpp"

namespace Shard::Engine::Core::Resources{

    using namespace Filesystem;

    void ResourcesManager::ConstructGlobalFileIndex(const Filesystem::Path &projectResDir)
    {
        Filesystem::Path projectDataBasePath = Core::GetEngine().GetCurrentProject()->GetAssetDatabasePath();
        Filesystem::Path engineDataBasePath = Core::GetEngine().GetFileManager()->GetEngineResRoot() / "asset_database.json";

        Serialization::DeserializeAssetDataBase(Core::GetEngine().GetFileManager()->GetEngineResRoot(), engineDataBasePath);
        
        Serialization::DeserializeAssetDataBase(projectResDir, projectDataBasePath);
        
    }

    std::shared_ptr<Rendering::Mesh> ResourcesManager::LoadModel(const std::string &pathInProject, const Filesystem::Path &path)
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

        std::shared_ptr<Rendering::Mesh> mesh = Rendering::Mesh::Create();
        mesh->CreateFromFBX(ufbx_mesh, scene->settings.unit_meters, scene->materials, mesh_node);
        meshes.emplace(pathInProject, mesh);
        mesh->SetAssetID(Core::GetEngine().GetAssetIDManager()->GetIDFromNameInProject(pathInProject));

        ufbx_free_scene(scene);

        return mesh;

    }

    std::shared_ptr<Rendering::Texture2D> ResourcesManager::LoadTexture(const std::string &pathInProject, const Filesystem::Path &path)
    {
        Rendering::TextureSpecifications specs = {};
        // Trilinear : mips are already generated (specs.generateMips defaults true) but the default
        // Linear min filter never samples them, so every material texture was minified straight from
        // level 0 - aliasing/shimmer on detailed textures at distance. GLTexture2D adds anisotropy on
        // top when mips are present.
        specs.minFilter = Rendering::TextureFilter::LinearMipmapLinear;
        std::shared_ptr<Rendering::Texture2D> img = Rendering::Texture2D::Create(specs,path);
        textures.emplace(pathInProject, img);
        img->SetAssetID(Core::GetEngine().GetAssetIDManager()->GetIDFromNameInProject(pathInProject));
        return img;
    }

    std::shared_ptr<Rendering::EnvironmentMap> ResourcesManager::LoadEnvMap(const std::string &pathInProject, const Filesystem::Path &path)
    {
        Rendering::TextureSpecifications specs = {};
        std::shared_ptr<Rendering::EnvironmentMap> envMap = Rendering::EnvironmentMap::Create(specs, path);
        if(!envMap){
            DEBUG_ERROR("Error during cubemap import");
            return nullptr;
        }
        envmaps.emplace(pathInProject, envMap);
        envMap->SetAssetID(Core::GetEngine().GetAssetIDManager()->GetIDFromNameInProject(pathInProject));
        return envMap;
    }

    std::shared_ptr<Rendering::Shader> ResourcesManager::LoadShader(const std::string &pathInProject, const Filesystem::Path &vsPath, const Filesystem::Path &fsPath, const Filesystem::Path &gsPath)
    {
        std::shared_ptr<Rendering::Shader> shader = Rendering::Shader::Create(vsPath, fsPath, gsPath);
        shader->SetAssetID(Core::GetEngine().GetAssetIDManager()->GetIDFromNameInProject(pathInProject+".vert"));
        shaders.emplace(pathInProject, shader);
        return shader;
    }

    std::shared_ptr<Rendering::ComputeShader> ResourcesManager::LoadComputeShader(const std::string &pathInProject, const Filesystem::Path &path)
    {
        std::shared_ptr<Rendering::ComputeShader> shader = Rendering::ComputeShader::Create(path);
        shader->SetAssetID(Core::GetEngine().GetAssetIDManager()->GetIDFromNameInProject(pathInProject+".comp"));
        computeShaders.emplace(pathInProject, shader);
        return shader;
    }

    std::shared_ptr<Rendering::Material> ResourcesManager::LoadMaterial(const std::string &pathInProject, const Filesystem::Path &path)
    {

        std::shared_ptr<Rendering::Material> mat = Serialization::DeserializeMaterial(path);
        if(!mat){
            DEBUG_ERROR("Error during material import.");
            return nullptr;
        }
        materials.emplace(pathInProject, mat);
        mat->SetAssetID(Core::GetEngine().GetAssetIDManager()->GetIDFromNameInProject(pathInProject));
        return mat;
    }

    std::shared_ptr<Levels::Level> ResourcesManager::LoadLevel(const std::string &pathInProject, const Filesystem::Path& path){

        std::shared_ptr<Levels::Level> level = std::make_shared<Levels::Level>("unitialized_level", path);
        int buildIndex = Core::GetEngine().GetBuildSettings()->GetLevelBuildIndex(pathInProject);
        if(buildIndex != -1){
            level->SetBuildIndex(buildIndex);
        }
        level->Deserialize(path);

        if(!level){
            DEBUG_ERROR("Unknown error during level import.");
            return nullptr;
        }
        auto [it, inserted] = levels.emplace(pathInProject, level);
        if (!inserted) {
            DEBUG_WARNING("Level '" + pathInProject + "' already loaded.");
        }
        level->SetAssetID(Core::GetEngine().GetAssetIDManager()->GetIDFromNameInProject(pathInProject));
        return level;
    }

    std::shared_ptr<Audio::SoundAsset> ResourcesManager::LoadSound(const std::string &pathInProject, const Filesystem::Path &path)
    {
        if (!path.Exists())
        {
            DEBUG_ERROR("Couldn't load sound: " + path.full);
            return nullptr;
        }

        std::shared_ptr<Audio::SoundAsset> sound = std::make_shared<Audio::SoundAsset>();
        sound->SetBuffer(path.ReadFile());
        sounds.emplace(pathInProject, sound);
        sound->SetAssetID(Core::GetEngine().GetAssetIDManager()->GetIDFromNameInProject(pathInProject));
        return sound;
    }

    std::shared_ptr<Rendering::Mesh> ResourcesManager::GetMesh(std::string pathInProject)
    {
        auto it = meshes.find(pathInProject);
        if (it != meshes.end())
            return it->second;
        else{
            Filesystem::AssetIDManager* assetManager = Core::GetEngine().GetAssetIDManager();
            std::shared_ptr<Filesystem::AssetInfos> assetInfos = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(pathInProject));
            
            if(assetInfos == nullptr){
                return nullptr;
            }

            return LoadModel(pathInProject, assetInfos->baseInfos.path);
        }
    }

    std::shared_ptr<Rendering::Material> ResourcesManager::GetMaterial(std::string pathInProject)
    {
        auto it = materials.find(pathInProject);
        if (it != materials.end())
            return it->second;
        else{
            Filesystem::AssetIDManager* assetManager = Core::GetEngine().GetAssetIDManager();
            std::shared_ptr<Filesystem::AssetInfos> assetInfos = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(pathInProject));

            if(assetInfos == nullptr) return nullptr;

            return LoadMaterial(pathInProject, assetInfos->baseInfos.path);
        }
    }

    std::shared_ptr<Rendering::Shader> ResourcesManager::GetShader(std::string pathInProject)
    {
        auto it = shaders.find(pathInProject);
        if (it != shaders.end()){
            return it->second;
        }
        else{
            Filesystem::AssetIDManager* assetManager = Core::GetEngine().GetAssetIDManager();
            std::shared_ptr<Filesystem::AssetInfos> assetInfos = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(pathInProject+".vert"));

            if(assetInfos == nullptr) return nullptr;

            Filesystem::Path vertPath = Filesystem::Path(assetInfos->baseInfos.path.GetParent()) / (assetInfos->baseInfos.name + ".vert");
            Filesystem::Path fragPath = Filesystem::Path(assetInfos->baseInfos.path.GetParent()) / (assetInfos->baseInfos.name + ".frag");
            Filesystem::Path geomPath = Filesystem::Path(assetInfos->baseInfos.path.GetParent()) / (assetInfos->baseInfos.name + ".geom");
            return LoadShader(pathInProject, vertPath, fragPath, geomPath.Exists() ? geomPath : Filesystem::Path(""));
        }
    }

    std::shared_ptr<Rendering::ComputeShader> ResourcesManager::GetComputeShader(std::string pathInProject)
    {
        auto it = computeShaders.find(pathInProject);
        if (it != computeShaders.end()){
            return it->second;
        }
        else{
            Filesystem::AssetIDManager* assetManager = Core::GetEngine().GetAssetIDManager();
            std::shared_ptr<Filesystem::AssetInfos> assetInfos = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(pathInProject+".comp"));

            if(assetInfos == nullptr) return nullptr;

            return LoadComputeShader(pathInProject, assetInfos->baseInfos.path);
        }
    }

    std::shared_ptr<Rendering::Texture2D> ResourcesManager::GetTexture(std::string pathInProject)
    {
        auto it = textures.find(pathInProject);
        if (it != textures.end())
            return it->second;
        else{
            Filesystem::AssetIDManager* assetManager = Core::GetEngine().GetAssetIDManager();
            std::shared_ptr<Filesystem::AssetInfos> assetInfos = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(pathInProject));

            if(assetInfos == nullptr) return nullptr;

            return LoadTexture(pathInProject, assetInfos->baseInfos.path);
        }
    }

    std::shared_ptr<Rendering::EnvironmentMap> ResourcesManager::GetEnvMap(std::string pathInProject)
    {
        auto it = envmaps.find(pathInProject);
        if (it != envmaps.end())
            return it->second;
        else{
            Filesystem::AssetIDManager* assetManager = Core::GetEngine().GetAssetIDManager();
            std::shared_ptr<Filesystem::AssetInfos> assetInfos = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(pathInProject));

            if(assetInfos == nullptr) return nullptr;

            return LoadEnvMap(pathInProject, assetInfos->baseInfos.path);
        }
    }

    std::shared_ptr<Levels::Level> ResourcesManager::GetLevel(const std::string &pathInProject)
    {
        if (auto it = levels.find(pathInProject); it != levels.end())
            return it->second;
        else{
            Filesystem::AssetIDManager* assetManager = Core::GetEngine().GetAssetIDManager();
            std::shared_ptr<Filesystem::AssetInfos> assetInfos = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(pathInProject));

            if(assetInfos == nullptr) return nullptr;

            return LoadLevel(pathInProject, assetInfos->baseInfos.path);
        }
    }

    std::shared_ptr<Audio::SoundAsset> ResourcesManager::GetSound(std::string pathInProject)
    {
        auto it = sounds.find(pathInProject);
        if (it != sounds.end())
            return it->second;
        else{
            Filesystem::AssetIDManager* assetManager = Core::GetEngine().GetAssetIDManager();
            std::shared_ptr<Filesystem::AssetInfos> assetInfos = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(pathInProject));

            if(assetInfos == nullptr) return nullptr;

            return LoadSound(pathInProject, assetInfos->baseInfos.path);
        }
    }

    std::shared_ptr<Rendering::ProbeBakeData> ResourcesManager::GetProbeBake(const std::string &pathInProject)
    {
        auto it = probeBakes.find(pathInProject);
        if (it != probeBakes.end())
            return it->second;

        Filesystem::AssetIDManager* assetManager = Core::GetEngine().GetAssetIDManager();
        std::shared_ptr<Filesystem::AssetInfos> assetInfos = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(pathInProject));

        if(assetInfos == nullptr || assetInfos->baseInfos.nameInProject != pathInProject){
            DEBUG_ERROR("Unknown probe bake : " + pathInProject);
            return nullptr;
        }

        std::shared_ptr<Rendering::ProbeBakeData> data = Rendering::DecodeProbeBakeFile(assetInfos->baseInfos.path);
        if(data)
            probeBakes.emplace(pathInProject, data);
        return data;
    }

    void ResourcesManager::AdoptProbeBake(const std::string &pathInProject, std::shared_ptr<Rendering::ProbeBakeData> probeBake)
    {
        probeBakes.emplace(pathInProject, probeBake);
    }

    bool ResourcesManager::HasProbeBake(const std::string &pathInProject) const
    {
        return probeBakes.find(pathInProject) != probeBakes.end();
    }

    void ResourcesManager::UnloadProbeBake(const std::string &name)
    {
        probeBakes.erase(name);
    }

    void ResourcesManager::AdoptMesh(const std::string &pathInProject, std::shared_ptr<Rendering::Mesh> mesh)
    {
        auto [it, inserted] = meshes.emplace(pathInProject, mesh);
        if(inserted){
            it->second->SetAssetID(Core::GetEngine().GetAssetIDManager()->GetIDFromNameInProject(pathInProject));
        }
    }

    void ResourcesManager::AdoptTexture(const std::string &pathInProject, std::shared_ptr<Rendering::Texture2D> texture)
    {
        auto [it, inserted] = textures.emplace(pathInProject, texture);
        if(inserted){
            it->second->SetAssetID(Core::GetEngine().GetAssetIDManager()->GetIDFromNameInProject(pathInProject));
        }
    }

    bool ResourcesManager::HasMesh(const std::string &pathInProject) const
    {
        return meshes.find(pathInProject) != meshes.end();
    }

    bool ResourcesManager::HasTexture(const std::string &pathInProject) const
    {
        return textures.find(pathInProject) != textures.end();
    }

    void ResourcesManager::UnLoadDependencies(const std::string &assetName)
    {

    }

    void ResourcesManager::UnloadMesh(const std::string &name)
    {
        auto it = meshes.find(name);
        if (it != meshes.end())
        {
            meshes.erase(it);
        }
    }

    void ResourcesManager::UnloadMaterial(const std::string &name)
    {
        auto it = materials.find(name);
        if (it != materials.end())
        {
            materials.erase(it);
        }
    }

    void ResourcesManager::UnloadShader(const std::string &name)
    {
        auto it = shaders.find(name);
        if (it != shaders.end())
        {
            shaders.erase(it);
        }
    }

    void ResourcesManager::UnloadComputeShader(const std::string &name)
    {
        auto it = computeShaders.find(name);
        if (it != computeShaders.end())
        {
            computeShaders.erase(it);
        }
    }

    void ResourcesManager::UnloadImage(const std::string &name)
    {
        auto it = textures.find(name);
        if (it != textures.end())
        {
            textures.erase(it);
        }
    }

    void ResourcesManager::UnloadEnvMap(const std::string &name)
    {
    }

    void ResourcesManager::UnloadLevel(const std::string &name)
    {
        auto it = levels.find(name);
        if (it != levels.end())
        {
            levels.erase(it);
        }
    }

    void ResourcesManager::UnloadSound(const std::string &name)
    {
        auto it = sounds.find(name);
        if (it != sounds.end())
        {
            sounds.erase(it);
        }
    }
}