#pragma once

#include "apps/editor/gui/panels/asset_editor.hpp"

#include <unordered_map>

namespace Shard::Editor::GUI{

    // Global Type -> IAssetEditor* lookup. Panels register themselves once at startup (they own their
    // own lifetime - the registry only keeps a non-owning pointer, mirroring how every other panel is
    // a raw pointer owned by EditorMainWindow); callers that need to open a double-clicked/dropped
    // asset go through TryOpen() instead of hardcoding which panel handles which extension.
    class AssetEditorRegistry
    {
        public:
            static AssetEditorRegistry& Instance()
            {
                static AssetEditorRegistry instance;
                return instance;
            }

            void Register(Engine::Filesystem::Type type, IAssetEditor* editor)
            {
                editors[type] = editor;
            }

            IAssetEditor* Get(Engine::Filesystem::Type type) const
            {
                auto it = editors.find(type);
                return it != editors.end() ? it->second : nullptr;
            }

            // Returns false (no-op) if no editor is registered for this asset's type.
            bool TryOpen(Engine::Filesystem::Type type, const Engine::Filesystem::Path& path)
            {
                IAssetEditor* editor = Get(type);
                if (!editor)
                    return false;

                editor->Open(path);
                return true;
            }

        private:
            std::unordered_map<Engine::Filesystem::Type, IAssetEditor*> editors;
    };
}
