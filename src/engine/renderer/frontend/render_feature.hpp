#pragma once

#include <cstdint>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "engine/renderer/rhi/render_snapshot.hpp"

namespace Shard::Engine::Rendering {

    class Renderer;
    class Framebuffer;

    struct PostProcessContext
    {
        uint32_t inputColor = 0;
        uint32_t depth = 0;
        std::shared_ptr<Framebuffer> output;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    class RenderFeature
    {
        public:
            virtual ~RenderFeature() = default;

            virtual const char* GetName() const = 0;

            virtual void Activate(Renderer& renderer) { (void)renderer; }
            virtual void Deactivate(Renderer& renderer) { (void)renderer; }

            virtual void BeforeFrame(Renderer& renderer, const RenderSnapshot& snapshot) { (void)renderer; (void)snapshot; }

            // Runs on the linear HDR scene before the tonemap. Draw into ctx.output with Renderer::DrawFullscreen, reading ctx.inputColor.
            virtual bool HasPostProcess() const { return false; }
            virtual int PostProcessOrder() const { return 0; }
            virtual void PostProcess(Renderer& renderer, const PostProcessContext& ctx) { (void)renderer; (void)ctx; }

            virtual void OnPassesCleared(Renderer& renderer) { (void)renderer; }
    };

    inline std::vector<std::shared_ptr<RenderFeature>> CollectPostProcessEffects(const std::vector<std::shared_ptr<RenderFeature>>& features)
    {
        std::vector<std::shared_ptr<RenderFeature>> effects;
        for (const std::shared_ptr<RenderFeature>& feature : features)
            if (feature && feature->HasPostProcess())
                effects.push_back(feature);
        std::stable_sort(effects.begin(), effects.end(), [](const auto& a, const auto& b) { return a->PostProcessOrder() < b->PostProcessOrder(); });
        return effects;
    }

    struct RenderPipelineExtension;
}
