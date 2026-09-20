#include "resources_manager.hpp"

#include <algorithm>

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

#include "engine/levels/level.hpp"

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
        // The material only stores its textures' raw GL handles, so it is the graph edge (not the
        // material's own pointers) that keeps them alive.
        SetDependencies({Kind::Material, pathInProject}, MaterialDependencyKeys(path));
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
        SetDependencies({Kind::Level, pathInProject}, LevelDependencyKeys(path));
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

    void ResourcesManager::AdoptSound(const std::string &pathInProject, std::shared_ptr<Audio::SoundAsset> sound)
    {
        auto [it, inserted] = sounds.emplace(pathInProject, sound);
        if(inserted){
            it->second->SetAssetID(Core::GetEngine().GetAssetIDManager()->GetIDFromNameInProject(pathInProject));
        }
    }

    bool ResourcesManager::HasSound(const std::string &pathInProject) const
    {
        return sounds.find(pathInProject) != sounds.end();
    }

    // ---------------------------------------------------------------------------------------------
    // Dependency graph / residency
    // ---------------------------------------------------------------------------------------------

    namespace {
        // Resolves a path in the project to its asset ID, only if it really is that asset (an unknown
        // name comes back as the default AssetID, which could coincide with a real one).
        bool ResolveAsset(const std::string& pathInProject, Filesystem::AssetID& outID)
        {
            auto* assetManager = Core::GetEngine().GetAssetIDManager();
            Filesystem::AssetID id = assetManager->GetIDFromNameInProject(pathInProject);
            auto info = assetManager->GetAssetFromID(id);
            if(!info || info->baseInfos.nameInProject != pathInProject)
                return false;
            outID = id;
            return true;
        }

        template<class Map>
        long UseCountOf(const Map& map, const std::string& path)
        {
            auto it = map.find(path);
            return it == map.end() ? 0 : it->second.use_count();
        }
    }

    std::vector<ResourcesManager::Key> ResourcesManager::LevelDependencyKeys(const Filesystem::Path &levelPath) const
    {
        std::vector<Key> keys;
        Levels::LevelAssetManifest manifest = Levels::CollectLevelAssetRefs(levelPath);
        for(auto& p : manifest.meshPathsInProject) keys.push_back({Kind::Mesh, p});
        for(auto& p : manifest.materialPathsInProject) keys.push_back({Kind::Material, p});
        for(auto& p : manifest.probeBakePathsInProject) keys.push_back({Kind::ProbeBake, p});
        for(auto& p : manifest.soundPathsInProject) keys.push_back({Kind::Sound, p});
        if(!manifest.skyboxEnvMapPathInProject.empty())
            keys.push_back({Kind::EnvMap, manifest.skyboxEnvMapPathInProject});
        return keys;
    }

    std::vector<ResourcesManager::Key> ResourcesManager::MaterialDependencyKeys(const Filesystem::Path &materialPath) const
    {
        std::vector<Key> keys;
        Serialization::MaterialAssetRefs refs = Serialization::PeekMaterialAssetRefs(materialPath);
        if(!refs.shaderPathInProject.empty())
            keys.push_back({Kind::Shader, refs.shaderPathInProject});
        for(auto& p : refs.texturePathsInProject) keys.push_back({Kind::Texture, p});
        return keys;
    }

    void ResourcesManager::SetDependencies(const Key &owner, const std::vector<Key> &deps)
    {
        ReleaseDependencies(owner);

        std::vector<Key> unique;
        std::unordered_set<Key, KeyHash> seen;
        for(auto& dep : deps){
            if(seen.insert(dep).second){
                unique.push_back(dep);
                retainCount[dep]++;
            }
        }
        dependencies[owner] = unique;

        StoreDependencies(owner, unique);
    }

    void ResourcesManager::StoreDependencies(const Key &owner, const std::vector<Key> &unique)
    {
        // Mirror into the asset database entry (persisted with the project). A shader is two/three files
        // there, hence the extra IDs.
        Filesystem::AssetID ownerID;
        if(!ResolveAsset(owner.path, ownerID))
            return;

        std::vector<Filesystem::AssetID> ids;
        auto addID = [&](const std::string& name){
            Filesystem::AssetID id;
            if(ResolveAsset(name, id) && std::find(ids.begin(), ids.end(), id) == ids.end())
                ids.push_back(id);
        };
        for(auto& dep : unique){
            if(dep.kind == Kind::Shader){
                addID(dep.path + ".vert");
                addID(dep.path + ".frag");
                addID(dep.path + ".geom");
            }
            else
                addID(dep.path);
        }
        Core::GetEngine().GetAssetIDManager()->GetAssetFromID(ownerID)->dependencies = ids;
    }

    void ResourcesManager::ReleaseDependencies(const Key &owner)
    {
        auto it = dependencies.find(owner);
        if(it == dependencies.end())
            return;

        for(auto& dep : it->second){
            auto count = retainCount.find(dep);
            if(count != retainCount.end() && --count->second <= 0)
                retainCount.erase(count);
            evictionCandidates.insert(dep);
        }
        dependencies.erase(it);
    }

    bool ResourcesManager::IsResident(const Key &key) const
    {
        switch(key.kind){
            case Kind::Mesh:          return meshes.count(key.path) > 0;
            case Kind::Texture:       return textures.count(key.path) > 0;
            case Kind::EnvMap:        return envmaps.count(key.path) > 0;
            case Kind::Shader:        return shaders.count(key.path) > 0;
            case Kind::ComputeShader: return computeShaders.count(key.path) > 0;
            case Kind::Material:      return materials.count(key.path) > 0;
            case Kind::Level:         return levels.count(key.path) > 0;
            case Kind::Sound:         return sounds.count(key.path) > 0;
            case Kind::ProbeBake:     return probeBakes.count(key.path) > 0;
        }
        return false;
    }

    bool ResourcesManager::IsEvictable(const Key &key) const
    {
        // Levels are owned by the LevelManager (UnloadLevel), never swept.
        if(key.kind == Kind::Level || retainCount.count(key) > 0)
            return false;

        long useCount = 0;
        switch(key.kind){
            case Kind::Mesh:          useCount = UseCountOf(meshes, key.path); break;
            case Kind::Texture:       useCount = UseCountOf(textures, key.path); break;
            case Kind::EnvMap:        useCount = UseCountOf(envmaps, key.path); break;
            case Kind::Shader:        useCount = UseCountOf(shaders, key.path); break;
            case Kind::ComputeShader: useCount = UseCountOf(computeShaders, key.path); break;
            case Kind::Material:      useCount = UseCountOf(materials, key.path); break;
            case Kind::Sound:         useCount = UseCountOf(sounds, key.path); break;
            case Kind::ProbeBake:     useCount = UseCountOf(probeBakes, key.path); break;
            default: break;
        }

        // Not resident (0) or still held by something besides the cache (Model, skybox, pipeline...)
        if(useCount != 1)
            return false;

        // Engine resources (IDs <= 500, see SerializeAssetDataBase) back the renderer itself and are
        // used by materials without being listed anywhere : always resident.
        std::string assetName = key.path;
        if(key.kind == Kind::Shader) assetName += ".vert";
        else if(key.kind == Kind::ComputeShader) assetName += ".comp";

        auto* assetManager = Core::GetEngine().GetAssetIDManager();
        Filesystem::AssetID id = assetManager->GetIDFromNameInProject(assetName);
        auto info = assetManager->GetAssetFromID(id);
        return info && info->baseInfos.nameInProject == assetName && id.GetAsInt() > 500;
    }

    void ResourcesManager::Evict(const Key &key)
    {
        switch(key.kind){
            case Kind::Mesh:          UnloadMesh(key.path); break;
            case Kind::Texture:       UnloadImage(key.path); break;
            case Kind::EnvMap:        UnloadEnvMap(key.path); break;
            case Kind::Shader:        UnloadShader(key.path); break;
            case Kind::ComputeShader: UnloadComputeShader(key.path); break;
            case Kind::Material:      UnloadMaterial(key.path); break;
            case Kind::Level:         UnloadLevel(key.path); break;
            case Kind::Sound:         UnloadSound(key.path); break;
            case Kind::ProbeBake:     UnloadProbeBake(key.path); break;
        }
    }

    int ResourcesManager::CollectUnused()
    {
        int evicted = 0;
        std::vector<Key> deferred;

        while(!evictionCandidates.empty()){
            Key key = *evictionCandidates.begin();
            evictionCandidates.erase(evictionCandidates.begin());

            if(!IsResident(key))
                continue;

            if(IsEvictable(key)){
                // Unload*() releases what this resource retained, which queues its dependencies here
                Evict(key);
                evicted++;
            }
            else if(retainCount.count(key) == 0 && key.kind != Kind::Level)
                deferred.push_back(key); // nobody needs it but something still holds it : try again next time
        }

        evictionCandidates.insert(deferred.begin(), deferred.end());
        return evicted;
    }

    void ResourcesManager::RefreshDependencies(const std::string &pathInProject)
    {
        auto* assetManager = Core::GetEngine().GetAssetIDManager();
        Filesystem::AssetID id;
        if(!ResolveAsset(pathInProject, id))
            return;

        auto info = assetManager->GetAssetFromID(id);

        Key owner{Kind::Level, pathInProject};
        std::vector<Key> keys;
        if(info->baseInfos.type == Filesystem::Type::T_LEVEL)
            keys = LevelDependencyKeys(info->baseInfos.path);
        else if(info->baseInfos.type == Filesystem::Type::T_MATERIAL){
            owner.kind = Kind::Material;
            keys = MaterialDependencyKeys(info->baseInfos.path);
        }
        else
            return;

        // Retains only make sense for a resident owner : nothing would ever release them otherwise
        if(IsResident(owner))
            SetDependencies(owner, keys);
        else{
            std::vector<Key> unique;
            for(auto& k : keys)
                if(std::find(unique.begin(), unique.end(), k) == unique.end())
                    unique.push_back(k);
            StoreDependencies(owner, unique);
        }
    }

    bool ResourcesManager::IsInUse(const std::string &pathInProject) const
    {
        std::string withoutExtension = pathInProject.substr(0, pathInProject.rfind('.'));

        const Kind plainKinds[] = {Kind::Mesh, Kind::Texture, Kind::EnvMap, Kind::Material, Kind::Level, Kind::Sound, Kind::ProbeBake};
        for(Kind kind : plainKinds){
            Key key{kind, pathInProject};
            if(retainCount.count(key) > 0)
                return true;
        }
        // Shaders are keyed without their .vert/.frag/.comp extension
        if(retainCount.count({Kind::Shader, withoutExtension}) > 0 || retainCount.count({Kind::ComputeShader, withoutExtension}) > 0)
            return true;

        return UseCountOf(meshes, pathInProject) > 1 || UseCountOf(textures, pathInProject) > 1
            || UseCountOf(envmaps, pathInProject) > 1 || UseCountOf(materials, pathInProject) > 1
            || UseCountOf(levels, pathInProject) > 1 || UseCountOf(sounds, pathInProject) > 1
            || UseCountOf(probeBakes, pathInProject) > 1
            || UseCountOf(shaders, withoutExtension) > 1 || UseCountOf(computeShaders, withoutExtension) > 1;
    }

    bool ResourcesManager::TryUnloadAsset(const std::string &pathInProject)
    {
        if(IsInUse(pathInProject))
            return false;

        std::string withoutExtension = pathInProject.substr(0, pathInProject.rfind('.'));

        UnloadMesh(pathInProject);
        UnloadImage(pathInProject);
        UnloadEnvMap(pathInProject);
        UnloadMaterial(pathInProject);
        UnloadLevel(pathInProject);
        UnloadSound(pathInProject);
        UnloadProbeBake(pathInProject);
        UnloadShader(withoutExtension);
        UnloadComputeShader(withoutExtension);

        CollectUnused();
        return true;
    }

    void ResourcesManager::UnLoadDependencies(const std::string &assetName)
    {
        Filesystem::AssetID id;
        if(!ResolveAsset(assetName, id))
            return;

        Filesystem::Type type = Core::GetEngine().GetAssetIDManager()->GetAssetFromID(id)->baseInfos.type;

        if(type == Filesystem::Type::T_LEVEL)
            UnloadLevel(assetName);
        else if(type == Filesystem::Type::T_MATERIAL){
            if(!IsEvictable({Kind::Material, assetName}))
                return;
            Evict({Kind::Material, assetName});
        }
        else
            return;

        CollectUnused();
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
        ReleaseDependencies({Kind::Material, name});
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
        envmaps.erase(name);
    }

    void ResourcesManager::UnloadLevel(const std::string &name)
    {
        auto it = levels.find(name);
        if (it != levels.end())
        {
            levels.erase(it);
        }
        ReleaseDependencies({Kind::Level, name});
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