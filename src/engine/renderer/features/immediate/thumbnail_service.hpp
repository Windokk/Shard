#pragma once

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"

#include <glm/glm.hpp>

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Shard::Engine::Rendering {

    class IRenderContext;
    class Mesh;

    enum class ThumbnailKind {
        Mesh,
        Material,
        Sound
    };

    using ThumbnailGenerator = bool (*)(const Filesystem::Path& file, std::vector<uint8_t>& rgba);

    struct ThumbnailRequest {
        ThumbnailKind kind = ThumbnailKind::Mesh;
        std::string nameInProject;
        Filesystem::Path filePath;
        ThumbnailGenerator generate = nullptr;
        uint32_t generatorVersion = 0;
    };

    struct ThumbnailRegion {
        glm::vec2 uv0;
        glm::vec2 uv1;
    };

    class ThumbnailService {
        public:
            static constexpr uint32_t kRenderVersion = 3;

            static constexpr uint32_t kCellSize = 128;
            static constexpr uint32_t kAtlasSize = 2048;
            static constexpr uint32_t kGridDim = kAtlasSize / kCellSize;

            static constexpr uint32_t kMaxRendersPerFrame = 2;
            static constexpr uint32_t kMaxDiskLoadsPerFrame = 16;
            static constexpr uint32_t kMaxGeneratorJobs = 2;

            void Init(IRenderContext* renderer);
            void Shutdown();

            void BeginFrame();

            bool Get(const ThumbnailRequest& request, ThumbnailRegion& out);

            void Invalidate(const ThumbnailRequest& request);

            uint32_t GetAtlasTexture() const { return m_Atlas ? m_Atlas->GetColorAttachment() : 0; }

        private:
            struct Entry {
                bool ready = false;
                uint32_t slot = 0;
                uint64_t lastUsedFrame = 0;
                std::string cacheFile;

                std::future<std::vector<uint8_t>> pending;
            };

            static std::string MakeKey(ThumbnailKind kind, const std::string& nameInProject);

            std::string CacheFilePath(const ThumbnailRequest& request) const;

            int AllocateSlot(const std::string& key);
            void FreeSlot(uint32_t slot);
            ThumbnailRegion RegionOf(uint32_t slot) const;

            bool TryLoadFromDisk(const std::string& cacheFile, uint32_t slot);
            bool Render(const ThumbnailRequest& request, uint32_t slot, const std::string& cacheFile);

            /// Starts drawing a Sound thumbnail on a worker thread. The entry then waits in m_Entries for FinishGenerated().
            void StartGenerated(const std::string& key, const ThumbnailRequest& request, const std::string& cacheFile);
            bool FinishGenerated(const std::string& key, Entry& entry, ThumbnailRegion& out);
            void StoreInAtlas(uint32_t slot, const uint8_t* rgba);

            std::shared_ptr<Mesh> GetPreviewSphere();

            IRenderContext* m_Renderer = nullptr;

            std::atomic<uint32_t> m_ActiveGenerators{0};

            std::shared_ptr<Framebuffer> m_Atlas;
            std::vector<std::string> m_SlotKeys;
            std::unordered_map<std::string, Entry> m_Entries;

            std::shared_ptr<Mesh> m_PreviewSphere;

            uint64_t m_Frame = 0;
            uint32_t m_RendersThisFrame = 0;
            uint32_t m_DiskLoadsThisFrame = 0;
    };
}
