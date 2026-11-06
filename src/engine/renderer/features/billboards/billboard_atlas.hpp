#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Rendering {

    class Texture2D;

    /// The icons the engine draws for its own component types. They are generated in code (no asset to ship or lose).
    enum class BuiltinSprite : uint32_t { Light = 0, Camera = 1, ProbeVolume = 2, Generic = 3, Count };

    // One 1024x1024 sheet of 64x64 tiles (256 sprites) so billboards are a single draw. Sprite id is the tile index, row 0 at the bottom, ids never change.
    class BillboardAtlas {
        public:
            static BillboardAtlas& Get();

            BillboardAtlas();

            static constexpr uint32_t kSheetSize = 1024;
            static constexpr uint32_t kTileSize = 64;
            static constexpr uint32_t kTilesPerRow = kSheetSize / kTileSize;
            static constexpr uint32_t kMaxSprites = kTilesPerRow * kTilesPerRow;

            uint32_t RegisterSheet(const std::string& key, uint32_t frames, uint32_t width, uint32_t height, const uint8_t* rgba);

            uint32_t RegisterFile(const Filesystem::Path& file, uint32_t frames);

            uint32_t LoadFolder(const std::string& folder);

            uint32_t Find(const std::string& name) const;

            uint32_t GetBuiltin(BuiltinSprite sprite) const { return static_cast<uint32_t>(sprite); }

            uint32_t GetSpriteCount() const;

            glm::vec4 GetUV(uint32_t sprite) const;

            bool Rebuild();

            std::shared_ptr<Texture2D> GetTexture() const;

            uint64_t GetVersion() const;

            static std::vector<uint8_t> MakeBuiltinPixels(BuiltinSprite sprite, uint32_t size);
            static constexpr uint32_t kBuiltinSize = kTileSize;

        private:
            struct Sprite {
                std::vector<uint8_t> rgba;
                glm::vec4 uv = glm::vec4(0.0f);
            };

            mutable std::mutex m_Mutex;
            std::vector<Sprite> m_Sprites;
            std::unordered_map<std::string, uint32_t> m_Sheets;
            std::unordered_map<std::string, uint32_t> m_Names;
            std::shared_ptr<Texture2D> m_Texture;
            uint32_t m_BuiltSprites = 0;
            uint64_t m_Version = 0;
    };
}
