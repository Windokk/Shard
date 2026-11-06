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

namespace Shard::Engine::Rendering {
    class Texture2D;
    class Framebuffer;
    class Material;
    class Mesh;
}

namespace Shard::Engine::Plugins::CAbi {

    struct TextureRec {
        PluginOwner owner = 0;
        uint32_t width = 0, height = 0, format = 0, flags = 0;
        std::string path;
        std::vector<uint8_t> pixels;
        bool decoded = true;
        bool failed = false;
        std::shared_ptr<Rendering::Texture2D> texture;
        std::shared_ptr<Rendering::Framebuffer> framebuffer;
        std::mutex mutex;
        struct Region { uint32_t x, y, w, h; std::vector<uint8_t> bytes; };
        std::vector<Region> regions;
    };

    std::shared_ptr<Rendering::Texture2D> ResolveTexture(PluginOwner owner, ShardTexture handle);
    std::shared_ptr<Rendering::Framebuffer> ResolveRenderTarget(PluginOwner owner, ShardTexture handle);

    // *flipV: the image is upside down for a UI toolkit
    uint64_t NativeTextureId(ShardTexture handle, bool* flipV);

    OwnedTable<Rendering::OverlayGeometry>& Geometries();

    void ExtractOverlays(const Rendering::RenderSnapshot& previous, Rendering::RenderSnapshot& next);
    void ReleaseOverlays(PluginOwner owner);

    void HookRenderRelease(PluginContext& ctx, PluginOwner owner);

    const void* DebugApi();
    const void* RenderApi();

    ShardResult SHARD_CALL OverlayAdd(ShardPluginCtx ctx, const ShardOverlayDesc* desc, ShardOverlay* out);
    ShardResult SHARD_CALL OverlayRemove(ShardPluginCtx ctx, ShardOverlay overlay);
    ShardResult SHARD_CALL GeometryCreate(ShardPluginCtx ctx, const ShardVertex2D* vertices, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount, ShardGeometry* out);
    ShardResult SHARD_CALL GeometryDestroy(ShardPluginCtx ctx, ShardGeometry geometry);

}
