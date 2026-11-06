#pragma once

#include <string>

#include "engine/assets/assetID.hpp"

namespace Shard::Engine::Audio
{
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
