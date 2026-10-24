#pragma once

#include "engine/renderer/rhi/render_snapshot.hpp"

namespace Shard::Engine::Plugins::CAbi {

    /// Where the viewport the overlays draw in sits in the window, in pixels (the editor's viewport) : what a UI subtracts from the positions of the
    /// input events. 0, 0 in a player.
    void SetPluginOverlayOrigin(float x, float y);

    /// The Extract stage's last word: puts what the plugins keep on screen (shard.render's objects, instance and sprite batches, overlays, shard.debug's
    /// shapes) into the snapshot being built. Main thread. Registered with RenderExtractor::AddSnapshotProvider.
    void ExtractPluginRender(const Rendering::RenderSnapshot& previous, Rendering::RenderSnapshot& next);
}
