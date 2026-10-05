#pragma once

#include "engine/filesystem/filesystem.hpp"

namespace Shard::Editor::GUI{

    // Common contract for a panel that owns the full edit lifecycle of one specific asset type -
    // load it from a path, draw its own ImGui window(s), save back to disk (e.g. MaterialEditorPanel
    // for .mat files). AssetEditorRegistry maps an Engine::Filesystem::Type to the IAssetEditor that
    // handles it, so callers that open assets (the asset browser's double-click handler, and anything
    // else that opens one in the future) don't special-case every asset type by name - they just ask
    // "does anyone handle this type?" and open through that.
    class IAssetEditor
    {
        public:
            virtual ~IAssetEditor() = default;

            // Loads the given asset and brings the editor's window to front.
            virtual void Open(const Engine::Filesystem::Path& path) = 0;

            // Draws the editor's own window(s). Called unconditionally every frame; implementations
            // are expected to no-op when not open, matching every other panel's Draw() convention.
            virtual void Draw() = 0;

            virtual bool IsOpen() const = 0;
    };
}
