#pragma once

#include <memory>
#include <string>

#include "engine/renderer/rhi/render_snapshot.hpp"

namespace Shard::Engine::Rendering {

    class Renderer;

    /// @brief A part of the frame a plugin adds : the "render.feature" extension point (`Render().AddFeature<RenderFeature>(name, feature)`).
    /// Its passes are registered on Activate and taken away on Deactivate ; each frame it is given the frame's snapshot, never the world.
    /// Everything here runs on the graphics thread. A feature is deactivated (and the renderer lets go of it) before the plugin that made it unloads.
    class RenderFeature
    {
        public:
            virtual ~RenderFeature() = default;

            virtual const char* GetName() const = 0;

            /// The feature joins the renderer : register its passes, build what it draws with
            virtual void Activate(Renderer& renderer) { (void)renderer; }
            /// It leaves : take its passes away, release what it holds
            virtual void Deactivate(Renderer& renderer) { (void)renderer; }

            /// Once per frame, after the scene was synchronised and before anything is drawn
            virtual void BeforeFrame(Renderer& renderer, const RenderSnapshot& snapshot) { (void)renderer; (void)snapshot; }

            /// The draw lists of the renderer were emptied (a world was reloaded) : what the feature keeps in them goes back
            virtual void OnPassesCleared(Renderer& renderer) { (void)renderer; }
    };

    /// A kind of render pipeline a plugin brings : the "render.pipeline" extension point (`Render().AddPipeline<RenderPipelineExtension>(name, item)`)
    struct RenderPipelineExtension;
}
