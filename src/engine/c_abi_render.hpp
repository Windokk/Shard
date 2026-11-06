#pragma once

#include "engine/renderer/rhi/render_snapshot.hpp"

namespace Shard::Engine::Plugins::CAbi {

    // Viewport origin in window pixels, subtracted from UI input positions. 0, 0 in a player.
    void SetPluginOverlayOrigin(float x, float y);

    void ExtractPluginRender(const Rendering::RenderSnapshot& previous, Rendering::RenderSnapshot& next);
}
