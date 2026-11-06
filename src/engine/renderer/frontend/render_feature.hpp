#pragma once

#include <memory>
#include <string>

#include "engine/renderer/rhi/render_snapshot.hpp"

namespace Shard::Engine::Rendering {

    class Renderer;

    class RenderFeature
    {
        public:
            virtual ~RenderFeature() = default;

            virtual const char* GetName() const = 0;

            virtual void Activate(Renderer& renderer) { (void)renderer; }
            virtual void Deactivate(Renderer& renderer) { (void)renderer; }

            virtual void BeforeFrame(Renderer& renderer, const RenderSnapshot& snapshot) { (void)renderer; (void)snapshot; }

            virtual void OnPassesCleared(Renderer& renderer) { (void)renderer; }
    };

    struct RenderPipelineExtension;
}
