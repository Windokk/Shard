#include "level_asset_prefetcher.hpp"

#include <unordered_set>
#include <chrono>

#include "engine/core/engine.hpp"
#include "engine/core/resources/resources_manager.hpp"
#include "engine/levels/level.hpp"
#include "engine/serialization/material/material_serializer.hpp"
#include "engine/debugging/logger.hpp"

namespace Shard::Engine::Levels{

    void AssetPrefetcher::BeginLoad(const std::string &pathInProject)
    {
        if(IsInProgress()){
            DEBUG_WARNING("Level load already in progress, ignoring request to load : " + pathInProject);
            return;
        }

        jobs.clear();
        completedCount = 0;
        totalCount = 0;

        auto* resources = Core::GetEngine().GetResourcesManager();
        auto* assetManager = Core::GetEngine().GetAssetIDManager();

        auto levelAssetInfos = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(pathInProject));
        if(!levelAssetInfos){
            DEBUG_ERROR("Cannot prefetch level, unknown asset : " + pathInProject);
            return;
        }

        LevelAssetManifest levelManifest = CollectLevelAssetRefs(levelAssetInfos->baseInfos.path);

        std::vector<std::string> texturePaths;
        for(auto& matPath : levelManifest.materialPathsInProject){
            auto matAssetInfos = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(matPath));
            if(!matAssetInfos)
                continue;

            Serialization::MaterialAssetRefs matRefs = Serialization::PeekMaterialAssetRefs(matAssetInfos->baseInfos.path);
            for(auto& texPath : matRefs.texturePathsInProject){
                texturePaths.push_back(texPath);
            }
        }

        std::unordered_set<std::string> seenMeshes, seenTextures;

        for(auto& p : levelManifest.meshPathsInProject){
            if(!seenMeshes.insert(p).second)
                continue;
            if(resources->HasMesh(p))
                continue;

            auto info = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(p));
            if(!info)
                continue;

            DecodeJob job;
            job.kind = DecodeKind::Mesh;
            job.pathInProject = p;
            Filesystem::Path path = info->baseInfos.path;
            job.meshFuture = std::async(std::launch::async, [path](){ return Rendering::DecodeMeshFile(path); });
            jobs.push_back(std::move(job));
        }

        for(auto& p : texturePaths){
            if(!seenTextures.insert(p).second)
                continue;
            if(resources->HasTexture(p))
                continue;

            auto info = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(p));
            if(!info)
                continue;

            DecodeJob job;
            job.kind = DecodeKind::Texture;
            job.pathInProject = p;
            Filesystem::Path path = info->baseInfos.path;
            job.textureFuture = std::async(std::launch::async, [path](){ return Rendering::DecodeTextureFile(path); });
            jobs.push_back(std::move(job));
        }

        // Baked probe volumes : same treatment as a texture - the file read + validation happens on a
        // worker thread, and the main thread only has to upload the result once the volume activates.
        // That is what keeps a baked level from stalling on a synchronous multi-megabyte read.
        std::unordered_set<std::string> seenProbeBakes;

        for(auto& p : levelManifest.probeBakePathsInProject){
            if(!seenProbeBakes.insert(p).second)
                continue;
            if(resources->HasProbeBake(p))
                continue;

            auto info = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(p));
            if(!info || info->baseInfos.nameInProject != p)
                continue;

            DecodeJob job;
            job.kind = DecodeKind::ProbeBake;
            job.pathInProject = p;
            Filesystem::Path path = info->baseInfos.path;
            job.probeBakeFuture = std::async(std::launch::async, [path](){ return Rendering::DecodeProbeBakeFile(path); });
            jobs.push_back(std::move(job));
        }

        totalCount = (int)jobs.size();
        state = State::Decoding;
    }

    bool AssetPrefetcher::Pump()
    {
        if(state != State::Decoding)
            return true;

        int completed = 0;
        bool allReady = true;
        for(auto& job : jobs){
            bool ready = false;
            switch(job.kind){
                case DecodeKind::Mesh:
                    ready = job.meshFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                    break;
                case DecodeKind::Texture:
                    ready = job.textureFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                    break;
                case DecodeKind::ProbeBake:
                    ready = job.probeBakeFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                    break;
            }

            if(ready) completed++;
            else allReady = false;
        }
        completedCount = completed;

        if(!allReady)
            return false;

        ApplyAll();

        state = State::Idle;
        return true;
    }

    void AssetPrefetcher::ApplyAll()
    {
        auto* resources = Core::GetEngine().GetResourcesManager();

        for(auto& job : jobs){
            if(job.kind == DecodeKind::Mesh){
                Rendering::MeshCPUData data = job.meshFuture.get();
                if(!data.success)
                    continue;

                std::shared_ptr<Rendering::Mesh> mesh = Rendering::Mesh::Create();
                mesh->CreateFromData(data);
                resources->AdoptMesh(job.pathInProject, mesh);
            }
            else if(job.kind == DecodeKind::ProbeBake){
                // No GL work to do here : the decoded CPU data just goes into the cache, and
                // ProbeVolume::Activate() uploads it when the level's volumes come up.
                std::shared_ptr<Rendering::ProbeBakeData> data = job.probeBakeFuture.get();
                if(data)
                    resources->AdoptProbeBake(job.pathInProject, data);
            }
            else{
                Rendering::TextureDecodeResult data = job.textureFuture.get();
                if(!data.success)
                    continue;

                Rendering::TextureSpecifications specs;
                specs.internalFormat = data.format;
                specs.width = data.width;
                specs.height = data.height;
                // Trilinear + (in GLTexture2D) anisotropy - mips are generated by default but the
                // default Linear min filter never uses them, so textures minified straight from level
                // 0 and shimmered at distance/grazing angles. Match ResourcesManager::LoadTexture.
                specs.minFilter = Rendering::TextureFilter::LinearMipmapLinear;

                std::shared_ptr<Rendering::Texture2D> texture = Rendering::Texture2D::Create(specs, data.pixels.data());
                resources->AdoptTexture(job.pathInProject, texture);
            }
        }

        jobs.clear();
    }

    float AssetPrefetcher::GetProgress() const
    {
        if(totalCount <= 0)
            return 1.0f;
        return (float)completedCount.load() / (float)totalCount;
    }
}
