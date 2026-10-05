#pragma once

#include <string>

#include "engine/assets/assetID.hpp"

namespace Shard::Engine::Audio
{
    // Cached raw encoded audio bytes for a sound file, keyed by project path in ResourcesManager -
    // shared by every AudioSource referencing the same asset instead of each one reading the file
    // from disk itself.
    class SoundAsset
    {
        public:
            void SetBuffer(std::string data) { buffer = std::move(data); }
            const std::string& GetBuffer() const { return buffer; }

            void SetAssetID(Filesystem::AssetID id) { assetID = id; }
            Filesystem::AssetID GetAssetID() const { return assetID; }

        private:
            std::string buffer;
            Filesystem::AssetID assetID;
    };
}
