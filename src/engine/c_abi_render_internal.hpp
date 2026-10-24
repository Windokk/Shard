#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <shard/shard_render.h>

#include "engine/plugins/abi/c_handles.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/renderer/rhi/render_snapshot.hpp"

// What the files of shard.render in the engine root share (c_abi_render.cpp : resources, objects, batches, camera ; c_abi_overlay.cpp : the 2D overlay
// and shard.debug). Not an API of the engine.

namespace Shard::Engine::Rendering {
    class Texture2D;
    class Framebuffer;
    class Material;
    class Mesh;
}

namespace Shard::Engine::Plugins::CAbi {

    /// A texture of a plugin. The CPU side is what the plugin made ; the GPU side is made on the main thread, at the first use.
    struct TextureRec {
        PluginOwner owner = 0;
        uint32_t width = 0, height = 0, format = 0, flags = 0;
        std::string path;                           // texture_load : the file, decoded at the first use
        std::vector<uint8_t> pixels;                // what is to be uploaded : the whole image at first, then regions
        bool decoded = true;                        // false : `path` is still to be read
        bool failed = false;
        std::shared_ptr<Rendering::Texture2D> texture;
        std::shared_ptr<Rendering::Framebuffer> framebuffer;    // a render target
        std::mutex mutex;
        struct Region { uint32_t x, y, w, h; std::vector<uint8_t> bytes; };
        std::vector<Region> regions;                // updates not applied yet
    };

    /// The GPU texture of a ShardTexture (null if it has none yet, or the handle is stale), made if need be. Main thread.
    std::shared_ptr<Rendering::Texture2D> ResolveTexture(PluginOwner owner, ShardTexture handle);
    /// The framebuffer behind a render-target texture, null for another kind
    std::shared_ptr<Rendering::Framebuffer> ResolveRenderTarget(PluginOwner owner, ShardTexture handle);

    /// The graphics API's id of a texture of any plugin (the editor shows them), 0 if it has none ; *flipV : the picture is upside down for a UI toolkit
    uint64_t NativeTextureId(ShardTexture handle, bool* flipV);

    /// 2D geometry of a plugin
    OwnedTable<Rendering::OverlayGeometry>& Geometries();

    /// The overlays and the debug shapes : append what they draw this frame to `next` (main thread, in the Extract stage)
    void ExtractOverlays(const Rendering::RenderSnapshot& previous, Rendering::RenderSnapshot& next);
    /// The plugin's context goes: its overlays, geometry and debug shapes with it
    void ReleaseOverlays(PluginOwner owner);

    /// Makes the plugin's render resources, overlays and geometry go with its context (the first thing it creates asks for it)
    void HookRenderRelease(PluginContext& ctx, PluginOwner owner);

    /// The table of shard.debug and of shard.render
    const void* DebugApi();
    const void* RenderApi();

    /// The overlay half of shard.render's table (c_abi_overlay.cpp)
    ShardResult SHARD_CALL OverlayAdd(ShardPluginCtx ctx, const ShardOverlayDesc* desc, ShardOverlay* out);
    ShardResult SHARD_CALL OverlayRemove(ShardPluginCtx ctx, ShardOverlay overlay);
    ShardResult SHARD_CALL GeometryCreate(ShardPluginCtx ctx, const ShardVertex2D* vertices, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount, ShardGeometry* out);
    ShardResult SHARD_CALL GeometryDestroy(ShardPluginCtx ctx, ShardGeometry geometry);

}
