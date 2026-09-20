#pragma once

#include "engine/filesystem/filesystem.hpp"
#include "engine/rendering/framebuffer/framebuffer.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Shard::Engine::Rendering {

    class Renderer;
    class Mesh;

    enum class ThumbnailKind {
        /// The mesh lit by the studio rig, with the default material.
        Mesh,
        /// The material on a preview sphere, lit by the studio rig.
        Material
    };

    struct ThumbnailRequest {
        ThumbnailKind kind = ThumbnailKind::Mesh;
        /// The asset's path in the project (what ResourcesManager loads it by).
        std::string nameInProject;
        /// The asset file on disk : its size and modification time decide when a cached thumbnail is stale.
        Filesystem::Path filePath;
    };

    /// @brief Where a thumbnail sits in the service's atlas texture. The V range is already flipped
    /// (uv0.y > uv1.y) so it can be passed straight to ImGui::Image / AddImage, like the main viewport texture.
    struct ThumbnailRegion {
        glm::vec2 uv0;
        glm::vec2 uv1;
    };

    /// @brief Asset thumbnails : rendered on demand through the ImmediateRenderer, packed into one atlas
    /// with least-recently-used eviction, and cached on disk (in the project) so they survive restarts.
    ///
    /// Usage : call BeginFrame() once per frame, then Get() for every thumbnail that is on screen. Get()
    /// never blocks for long : while a thumbnail is not ready it returns false (draw a generic icon and ask
    /// again next frame). Like the ImmediateRenderer it must run on the main thread, between the frame's
    /// scene render and the UI draw.
    class ThumbnailService {
        public:
            /// Bump when the look of thumbnails changes (studio lights, framing, ...) : it is part of the
            /// disk cache key, so old cached images are ignored rather than shown stale.
            static constexpr uint32_t kRenderVersion = 1;

            static constexpr uint32_t kCellSize = 128;
            static constexpr uint32_t kAtlasSize = 2048;
            static constexpr uint32_t kGridDim = kAtlasSize / kCellSize;

            /// Cap on fresh renders per frame (they cost a draw + a GPU readback each) ...
            static constexpr uint32_t kMaxRendersPerFrame = 2;
            /// ... and on cache-file loads (a decode + an upload each).
            static constexpr uint32_t kMaxDiskLoadsPerFrame = 16;

            void Init(Renderer* renderer);
            void Shutdown();

            void BeginFrame();

            /// @return true and fills `out` when the thumbnail is in the atlas
            bool Get(const ThumbnailRequest& request, ThumbnailRegion& out);

            /// @brief Forces the next Get() for this asset to re-render it : drops it from the atlas and
            /// deletes its disk cache file (also when it wasn't loaded this session, and also clearing a
            /// remembered failure). Call it after the asset was edited, or when the user asks for a refresh.
            void Invalidate(const ThumbnailRequest& request);

            /// @brief GL texture id of the atlas. Stable for the service's lifetime.
            uint32_t GetAtlasTexture() const { return m_Atlas ? m_Atlas->GetColorAttachment() : 0; }

        private:
            struct Entry {
                bool ready = false;      // false = a render/load already failed, don't retry until Invalidate()
                uint32_t slot = 0;
                uint64_t lastUsedFrame = 0;
                std::string cacheFile;   // empty when there is no disk cache for it
            };

            static std::string MakeKey(ThumbnailKind kind, const std::string& nameInProject);

            std::string CacheFilePath(const ThumbnailRequest& request) const;

            /// @return slot index, or -1 if every slot was used this frame (nothing safe to evict)
            int AllocateSlot(const std::string& key);
            void FreeSlot(uint32_t slot);
            ThumbnailRegion RegionOf(uint32_t slot) const;

            bool TryLoadFromDisk(const std::string& cacheFile, uint32_t slot);
            bool Render(const ThumbnailRequest& request, uint32_t slot, const std::string& cacheFile);

            std::shared_ptr<Mesh> GetPreviewSphere();

            Renderer* m_Renderer = nullptr;

            std::shared_ptr<Framebuffer> m_Atlas;
            std::vector<std::string> m_SlotKeys;   // owner key per slot, empty = free
            std::unordered_map<std::string, Entry> m_Entries;

            std::shared_ptr<Mesh> m_PreviewSphere;

            uint64_t m_Frame = 0;
            uint32_t m_RendersThisFrame = 0;
            uint32_t m_DiskLoadsThisFrame = 0;
    };
}
