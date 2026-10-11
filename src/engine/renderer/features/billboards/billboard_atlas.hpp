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

    /// @brief Every sprite the billboards draw, in one spritesheet texture so that the whole lot is a single draw call.
    /// The sheet is a fixed 1024 x 1024 grid of 64 x 64 pixels tiles (16 x 16 = 256 sprites) ; a sprite id is its tile index
    /// (row by row, row 0 at the bottom). Every image is resampled to one tile when it is registered.
    /// @note Registration (main thread, CPU only : the pixels are decoded by the caller) and Rebuild (graphics thread : makes the
    /// texture) are separate, and the registry is mutex protected. A sprite id never changes once given.
    class BillboardAtlas {
        public:
            static BillboardAtlas& Get();

            BillboardAtlas();

            static constexpr uint32_t kSheetSize = 1024;
            static constexpr uint32_t kTileSize = 64;
            static constexpr uint32_t kTilesPerRow = kSheetSize / kTileSize;
            static constexpr uint32_t kMaxSprites = kTilesPerRow * kTilesPerRow;

            /// Registers a sprite sheet of `frames` equal cells laid out in a row (width / frames each), each resampled to a tile.
            /// The same key answers the same first sprite id again. Answers the generic icon when the atlas is full. @return the id of the first frame ; frame n is first + n.
            uint32_t RegisterSheet(const std::string& key, uint32_t frames, uint32_t width, uint32_t height, const uint8_t* rgba);

            /// Decodes an image of the project and registers it as a sheet. Falls back to the generic icon when it cannot be read.
            uint32_t RegisterFile(const Filesystem::Path& file, uint32_t frames);

            /// Registers every image of a folder (not recursive) as one sprite named after its file (no extension). Runtime loading,
            /// main thread. @return the number of sprites added
            uint32_t LoadFolder(const std::string& folder);

            /// The sprite registered under a name by LoadFolder (case insensitive), or the generic icon
            uint32_t Find(const std::string& name) const;

            uint32_t GetBuiltin(BuiltinSprite sprite) const { return static_cast<uint32_t>(sprite); }

            uint32_t GetSpriteCount() const;

            /// u0, v0, u1, v1 of a sprite in the atlas as last built (an unknown id answers the generic icon's)
            glm::vec4 GetUV(uint32_t sprite) const;

            /// Packs the sprites registered since the last call into the texture. Graphics thread. @return whether the texture changed
            bool Rebuild();

            std::shared_ptr<Texture2D> GetTexture() const;

            /// Grows with every Rebuild that changed something
            uint64_t GetVersion() const;

            /// The pixels of a built-in icon (size x size RGBA), exposed for the tests
            static std::vector<uint8_t> MakeBuiltinPixels(BuiltinSprite sprite, uint32_t size);
            static constexpr uint32_t kBuiltinSize = kTileSize;

        private:
            struct Sprite {
                std::vector<uint8_t> rgba;          // kTileSize x kTileSize
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
