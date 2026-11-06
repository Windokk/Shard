#pragma once

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Editor::GUI{

    class IAssetEditor
    {
        public:
            virtual ~IAssetEditor() = default;

            virtual void Open(const Engine::Filesystem::Path& path) = 0;

            virtual void Draw() = 0;

            virtual bool IsOpen() const = 0;
    };
}
