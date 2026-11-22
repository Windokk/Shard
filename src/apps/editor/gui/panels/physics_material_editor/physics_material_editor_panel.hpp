#pragma once

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/collision/collision_profile.hpp"

#include "apps/editor/gui/panels/asset_editor.hpp"

#include <string>

namespace Shard::Editor::GUI{

    class PhysicsMaterialEditorPanel : public IAssetEditor
    {
        public:
            void Open(const Engine::Filesystem::Path& path) override;
            void Draw() override;
            bool IsOpen() const override { return isOpen; }

        private:
            void Load();
            bool Save();

            bool isOpen = false;
            bool focusRequested = false;
            bool dirty = false;
            bool loaded = false;

            Engine::Filesystem::Path path;
            std::string nameInProject;
            Engine::Collision::PhysicsMaterial material;
    };
}
