#include "world_asset_prefetcher.hpp"

#include <unordered_set>
#include <chrono>
#include <thread>
#include <algorithm>

#include "engine/world/engine.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Worlds{

    void AssetPrefetcher::BeginLoad(const std::string &pathInProject)
    {
        if(IsInProgress()){
            DEBUG_WARNING("World load already in progress, ignoring request to load : " + pathInProject);
            return;
        }

        jobs.clear();
        maxInFlight = std::max(2, (int)std::thread::hardware_concurrency() - 1);
        completedCount = 0;
        totalCount = 0;

        auto* resources = Core::GetEngine().GetResourcesManager();
        auto* assetManager = Core::GetEngine().GetAssetIDManager();

        auto worldAssetInfos = assetManager->GetAssetFromID(assetManager->GetIDFromNameInProject(pathInProject));
        if(!worldAssetInfos){
            DEBUG_ERROR("Cannot prefetch world, unknown asset : " + pathInProject);
            return;
        }

        for(auto& task : resources->PlanPrefetch(pathInProject)){
            DecodeJob job;
            job.task = std::move(task);
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
        return job.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    void AssetPrefetcher::StartDecode(DecodeJob& job)
    {
        job.future = std::async(std::launch::async, job.task.decode);
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
        std::function<void()> finish = job.future.get();
        if(finish)
            finish();
    }

    float AssetPrefetcher::GetProgress() const
    {
        if(totalCount <= 0)
            return 1.0f;
        return (float)completedCount.load() / (float)totalCount;
    }
}
