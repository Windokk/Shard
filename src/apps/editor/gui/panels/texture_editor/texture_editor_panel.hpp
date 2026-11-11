#pragma once

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/resources/texture/texture_settings.hpp"

#include "apps/editor/gui/panels/asset_editor.hpp"

#include <imgui/imgui.h>

#include <memory>
#include <string>
#include <vector>

namespace Shard::Editor::GUI{

    class TextureEditorPanel : public IAssetEditor
    {
        public:
            void Open(const Engine::Filesystem::Path& path) override;
            void Draw() override;
            bool IsOpen() const override { return isOpen; }

        private:
            enum class Backdrop{
                Checker,
                Black,
                Grey,
                White
            };

            void Load(const Engine::Filesystem::Path& newPath);
            void Save();
            void Revert();
            void ReloadResident();

            void DrawToolbar();
            void DrawViewer(const std::shared_ptr<Engine::Rendering::Texture2D>& texture);
            void DrawStatusLine();
            void DrawInformation();
            void DrawSizeSettings();
            void DrawSamplingSettings();
            void DrawActions();

            void ResolvedSize(uint32_t& width, uint32_t& height) const;
            void FitToView();
            bool SampleSource(float u, float v, unsigned char out[4], int& x, int& y) const;

            bool isOpen = false;
            bool focusRequested = false;

            Engine::Filesystem::Path path;
            std::string nameInProject;

            Engine::Rendering::TextureImportSettings saved;
            Engine::Rendering::TextureImportSettings edited;
            Engine::Rendering::TextureFileInfo source;
            int fileSize = 0;

            Engine::Rendering::TextureDecodeResult probe;

            bool keepAspect = true;

            float zoom = 1.0f;
            ImVec2 pan = ImVec2(0.0f, 0.0f);
            bool fitPending = true;
            ImVec2 canvasSize = ImVec2(0.0f, 0.0f);

            bool showRed = true;
            bool showGreen = true;
            bool showBlue = true;
            bool showGrid = false;
            bool tiled = false;
            Backdrop backdrop = Backdrop::Checker;

            bool hoverValid = false;
            float hoverU = 0.0f;
            float hoverV = 0.0f;

            bool IsDirty() const { return edited != saved; }
    };
}
