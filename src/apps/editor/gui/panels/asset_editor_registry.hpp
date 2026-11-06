#pragma once

#include "apps/editor/gui/panels/asset_editor.hpp"

#include <unordered_map>

namespace Shard::Editor::GUI{

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
