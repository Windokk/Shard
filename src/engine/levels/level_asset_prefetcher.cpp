#include "level_asset_prefetcher.hpp"

#include <unordered_set>
#include <chrono>
#include <thread>
#include <algorithm>

#include "engine/core/engine.hpp"
#include "engine/core/resources/resources_manager.hpp"
#include "engine/levels/level.hpp"
#include "engine/audio/sound_asset.hpp"
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
        maxInFlight = std::max(2, (int)std::thread::hardware_concurrency() - 1);
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
            job.path = info->baseInfos.path;
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
            job.path = info->baseInfos.path;
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
            job.path = info->baseInfos.path;
            jobs.push_back(std::move(job));
        }

        // Sounds : the (multi-megabyte) file read moves off the main thread, so the first Play() of an
        // AudioSource finds the bytes already cached instead of blocking on the disk.
        std::unordered_set<std::string> seenSounds;

        for(auto& p : levelManifest.soundPathsInProject){
            if(!seenSounds.insert(p).second)
                continue;
            if(resources->HasSound(p))
                continue;

            auto info = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(p));
            if(!info || info->baseInfos.nameInProject != p)
                continue;

            DecodeJob job;
            job.kind = DecodeKind::Sound;
            job.pathInProject = p;
            job.path = info->baseInfos.path;
            jobs.push_back(std::move(job));
        }

        totalCount = (int)jobs.size();
        state = State::Decoding;
        // Kick off the first decodes right away instead of waiting for the first Pump()
        for(int i = 0; i < (int)jobs.size() && i < maxInFlight; i++)
            StartDecode(jobs[i]);
    }

    bool AssetPrefetcher::IsDecoded(DecodeJob& job)
    {
        switch(job.kind){
            case DecodeKind::Mesh:      return job.meshFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
            case DecodeKind::Texture:   return job.textureFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
            case DecodeKind::ProbeBake: return job.probeBakeFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
            case DecodeKind::Sound:     return job.soundFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }
        return true;
    }

    void AssetPrefetcher::StartDecode(DecodeJob& job)
    {
        Filesystem::Path path = job.path;
        switch(job.kind){
            case DecodeKind::Mesh:
                job.meshFuture = std::async(std::launch::async, [path](){ return Rendering::DecodeMeshFile(path); });
                break;
            case DecodeKind::Texture:
                job.textureFuture = std::async(std::launch::async, [path](){ return Rendering::DecodeTextureFile(path); });
                break;
            case DecodeKind::ProbeBake:
                job.probeBakeFuture = std::async(std::launch::async, [path](){ return Rendering::DecodeProbeBakeFile(path); });
                break;
            case DecodeKind::Sound:
                job.soundFuture = std::async(std::launch::async, [path](){ return path.ReadFile(); });
                break;
        }
        job.started = true;
    }

    bool AssetPrefetcher::Pump(float uploadBudgetMs)
    {
        if(state != State::Decoding)
            return true;

        using Clock = std::chrono::steady_clock;
        const auto begin = Clock::now();
        const bool unlimited = uploadBudgetMs < 0.0f;

        int applied = 0;
        int inFlight = 0;
        bool uploadedAny = false;

        for(auto& job : jobs){
            if(job.applied){
                applied++;
                continue;
            }

            if(!job.started)
                continue;

            if(!IsDecoded(job)){
                inFlight++;
                continue;
            }

            // Decoded : upload it, unless this frame's budget is already gone (it then waits, decoded,
            // for the next call). The first upload of a call is always allowed.
            if(!unlimited && uploadedAny && std::chrono::duration<float, std::milli>(Clock::now() - begin).count() >= uploadBudgetMs){
                continue;
            }

            Apply(job);
            job.applied = true;
            uploadedAny = true;
            applied++;
        }

        // Keep the worker threads busy with the next assets in line
        for(auto& job : jobs){
            if(inFlight >= maxInFlight)
                break;
            if(job.started)
                continue;
            StartDecode(job);
            inFlight++;
        }

        completedCount = applied;

        if(applied < (int)jobs.size())
            return false;

        jobs.clear();
        state = State::Idle;
        return true;
    }

    void AssetPrefetcher::Apply(DecodeJob& job)
    {
        auto* resources = Core::GetEngine().GetResourcesManager();

        if(job.kind == DecodeKind::Mesh){
            Rendering::MeshCPUData data = job.meshFuture.get();
            if(!data.success)
                return;

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
        else if(job.kind == DecodeKind::Sound){
            std::string bytes = job.soundFuture.get();
            if(bytes.empty())
                return;

            std::shared_ptr<Audio::SoundAsset> sound = std::make_shared<Audio::SoundAsset>();
            sound->SetBuffer(std::move(bytes));
            resources->AdoptSound(job.pathInProject, sound);
        }
        else{
            Rendering::TextureDecodeResult data = job.textureFuture.get();
            if(!data.success)
                return;

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

    float AssetPrefetcher::GetProgress() const
    {
        if(totalCount <= 0)
            return 1.0f;
        return (float)completedCount.load() / (float)totalCount;
    }
}
