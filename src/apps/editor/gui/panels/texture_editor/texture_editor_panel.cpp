#include "texture_editor_panel.hpp"

#include "engine/world/engine.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/assets/hot_reloader.hpp"
#include "engine/core/diagnostics/logger.hpp"

#include "apps/editor/gui/notifications.hpp"
#include "apps/editor/gui/popups.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Shard::Editor::GUI{

    using namespace Shard::Engine;
    using Rendering::TextureFilter;
    using Rendering::TextureImportSettings;
    using Rendering::TextureSizeMode;
    using Rendering::TextureWrap;

    namespace {

        constexpr float kMinZoom = 0.02f;
        constexpr float kMaxZoom = 64.0f;
        constexpr uint32_t kMaxSide = 16384;
        constexpr uint64_t kMaxProbePixels = 8192ull * 8192ull;

        const TextureFilter kMinFilterValues[] = {
            TextureFilter::Nearest, TextureFilter::Linear,
            TextureFilter::NearestMipmapNearest, TextureFilter::NearestMipmapLinear,
            TextureFilter::LinearMipmapNearest, TextureFilter::LinearMipmapLinear
        };
        const char* const kMinFilterNames[] = {
            "Nearest", "Linear",
            "Nearest, mip nearest", "Nearest, mip linear",
            "Linear, mip nearest", "Linear, mip linear (trilinear)"
        };

        const TextureFilter kMagFilterValues[] = { TextureFilter::Nearest, TextureFilter::Linear };
        const char* const kMagFilterNames[] = { "Nearest", "Linear" };

        const TextureWrap kWrapValues[] = { TextureWrap::Repeat, TextureWrap::ClampEdge, TextureWrap::ClampBorder, TextureWrap::Mirror };
        const char* const kWrapNames[] = { "Repeat", "Clamp to edge", "Clamp to border", "Mirror" };

        const TextureSizeMode kSizeModeValues[] = {
            TextureSizeMode::Original, TextureSizeMode::NearestPowerOfTwo, TextureSizeMode::NextPowerOfTwo,
            TextureSizeMode::PreviousPowerOfTwo, TextureSizeMode::Custom
        };
        const char* const kSizeModeNames[] = {
            "Original", "Nearest power of 2", "Next power of 2", "Previous power of 2", "Custom"
        };

        const uint32_t kMaxSizes[] = { 0, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192 };

        template<class T, size_t N>
        bool EnumCombo(const char* id, T& value, const T (&values)[N], const char* const (&names)[N])
        {
            int current = 0;
            for (size_t i = 0; i < N; i++)
                if (values[i] == value) current = int(i);

            bool changed = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo(id, names[current]))
            {
                for (size_t i = 0; i < N; i++)
                {
                    if (ImGui::Selectable(names[i], int(i) == current))
                    {
                        value = values[i];
                        changed = true;
                    }
                    if (int(i) == current)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        std::string FormatBytes(uint64_t bytes)
        {
            char buffer[32];
            if (bytes >= 1024ull * 1024ull)
                std::snprintf(buffer, sizeof(buffer), "%.2f MB", double(bytes) / (1024.0 * 1024.0));
            else if (bytes >= 1024ull)
                std::snprintf(buffer, sizeof(buffer), "%.1f KB", double(bytes) / 1024.0);
            else
                std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
            return buffer;
        }

        uint32_t ChannelsOf(Rendering::TextureInternalFormat format)
        {
            switch (format)
            {
                case Rendering::TextureInternalFormat::RED:  return 1;
                case Rendering::TextureInternalFormat::RG:   return 2;
                case Rendering::TextureInternalFormat::RGB:  return 3;
                default:                                     return 4;
            }
        }

        void Row(const char* label)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", label);
            ImGui::TableSetColumnIndex(1);
        }

        bool BeginRows(const char* id)
        {
            if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
                return false;
            ImGui::TableSetupColumn("##Label", ImGuiTableColumnFlags_WidthStretch, 0.38f);
            ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch, 0.62f);
            return true;
        }
    }

    void TextureEditorPanel::Open(const Filesystem::Path& newPath)
    {
        if (isOpen && IsDirty() && newPath.full != path.full)
        {
            Popups::ConfirmUnsavedChanges(path.GetFilename(),
                [this, newPath](){ Save(); Load(newPath); },
                [this, newPath](){ Load(newPath); });
            return;
        }

        Load(newPath);
    }

    void TextureEditorPanel::Load(const Filesystem::Path& newPath)
    {
        path = newPath;
        isOpen = true;
        focusRequested = true;
        fitPending = true;
        hoverValid = false;
        pan = ImVec2(0.0f, 0.0f);

        nameInProject = Core::GetEngine().GetFileManager()->GetFileInfos(path).nameInProject;

        saved = Rendering::LoadTextureSettings(path);
        edited = saved;

        source = Rendering::ReadTextureFileInfo(path);
        fileSize = path.GetFileSize();

        probe = Rendering::TextureDecodeResult();
        if (source.valid && uint64_t(source.width) * source.height <= kMaxProbePixels)
            probe = Rendering::DecodeTextureFile(path);
    }

    void TextureEditorPanel::Revert()
    {
        edited = saved;
    }

    void TextureEditorPanel::ReloadResident()
    {
        auto& engine = Core::GetEngine();

        if (auto* reloader = engine.GetHotReloader())
        {
            reloader->ReloadAsset(nameInProject);
            return;
        }

        auto* resources = engine.GetResourcesManager();
        resources->Reload(Core::Resources::AssetKind::Texture, nameInProject);
        resources->Reload(Core::Resources::AssetKind::ColorTexture, nameInProject);
    }

    void TextureEditorPanel::Save()
    {
        if (!Rendering::SaveTextureSettings(path, edited))
        {
            Notifications::Error("Couldn't save the settings of %s", path.GetFilename().c_str());
            return;
        }

        saved = edited;
        ReloadResident();
        Notifications::Success("Texture settings applied to %s", path.GetFilename().c_str());
    }

    void TextureEditorPanel::ResolvedSize(uint32_t& width, uint32_t& height) const
    {
        Rendering::ResolveTextureSize(edited, source.width, source.height, width, height);
    }

    void TextureEditorPanel::FitToView()
    {
        fitPending = true;
    }

    bool TextureEditorPanel::SampleSource(float u, float v, unsigned char out[4], int& x, int& y) const
    {
        if (!probe.success || probe.width <= 0 || probe.height <= 0)
            return false;

        const int width = probe.width;
        const int height = probe.height;
        const uint32_t channels = ChannelsOf(probe.format);
        if (probe.pixels.size() < size_t(width) * height * channels)
            return false;

        x = std::clamp(int(std::floor(u * width)), 0, width - 1);
        y = std::clamp(int(std::floor(v * height)), 0, height - 1);

        const unsigned char* texel = probe.pixels.data() + (size_t(height - 1 - y) * width + x) * channels;
        switch (channels)
        {
            case 1: out[0] = out[1] = out[2] = texel[0]; out[3] = 255; break;
            case 2: out[0] = texel[0]; out[1] = texel[1]; out[2] = 0; out[3] = 255; break;
            case 3: out[0] = texel[0]; out[1] = texel[1]; out[2] = texel[2]; out[3] = 255; break;
            default: out[0] = texel[0]; out[1] = texel[1]; out[2] = texel[2]; out[3] = texel[3]; break;
        }
        return true;
    }

    void TextureEditorPanel::Draw()
    {
        if (!isOpen)
            return;

        ImGui::SetNextWindowSize(ImVec2(960, 620), ImGuiCond_FirstUseEver);
        if (focusRequested)
        {
            ImGui::SetNextWindowFocus();
            focusRequested = false;
        }

        const std::string title = "Texture Editor - " + path.GetFilename() + "###TextureEditor";
        const bool wasOpen = isOpen;

        if (ImGui::Begin(title.c_str(), &isOpen))
        {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const float settingsWidth = std::min(330.0f, available.x * 0.45f);
            const float spacing = ImGui::GetStyle().ItemSpacing.x;

            if (ImGui::BeginChild("##TextureView", ImVec2(std::max(50.0f, available.x - settingsWidth - spacing), 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
            {
                DrawToolbar();

                std::shared_ptr<Rendering::Texture2D> texture = Core::GetEngine().GetResourcesManager()->Get<Rendering::Texture2D>(Core::Resources::AssetKind::Texture, nameInProject);
                DrawViewer(texture);
                DrawStatusLine();
            }
            ImGui::EndChild();

            ImGui::SameLine();

            if (ImGui::BeginChild("##TextureSettings", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
            {
                DrawInformation();
                DrawSizeSettings();
                DrawSamplingSettings();
                DrawActions();
            }
            ImGui::EndChild();
        }
        ImGui::End();

        if (wasOpen && !isOpen && IsDirty())
        {
            isOpen = true;

            Popups::ConfirmUnsavedChanges(path.GetFilename(),
                [this](){ Save(); isOpen = false; },
                [this](){ edited = saved; isOpen = false; });
        }

        if (!isOpen)
            probe = Rendering::TextureDecodeResult();
    }

    void TextureEditorPanel::DrawToolbar()
    {
        if (ImGui::Button("Fit"))
            FitToView();
        ImGui::SameLine();
        if (ImGui::Button("100%"))
        {
            zoom = 1.0f;
            pan = ImVec2(0.0f, 0.0f);
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        auto channelButton = [](const char* label, bool& value, ImU32 color){
            ImGui::PushStyleColor(ImGuiCol_Text, value ? color : ImGui::GetColorU32(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(label, value, 0, ImVec2(ImGui::GetFrameHeight(), 0.0f)))
                value = !value;
            ImGui::PopStyleColor();
        };

        channelButton("R", showRed, IM_COL32(255, 90, 90, 255));
        ImGui::SameLine();
        channelButton("G", showGreen, IM_COL32(90, 230, 110, 255));
        ImGui::SameLine();
        channelButton("B", showBlue, IM_COL32(100, 150, 255, 255));

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        ImGui::Checkbox("Tiled", &tiled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shows the texture 3 x 3 times, to check that it repeats without a seam.");
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &showGrid);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Pixel grid, from 600% zoom.");

        ImGui::SameLine();
        static const char* kBackdrops[] = { "Checker", "Black", "Grey", "White" };
        int backdropIndex = int(backdrop);
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::Combo("##Backdrop", &backdropIndex, kBackdrops, IM_ARRAYSIZE(kBackdrops)))
            backdrop = Backdrop(backdropIndex);
    }

    void TextureEditorPanel::DrawViewer(const std::shared_ptr<Rendering::Texture2D>& texture)
    {
        const float statusHeight = ImGui::GetFrameHeightWithSpacing();
        ImVec2 size = ImGui::GetContentRegionAvail();
        size.y = std::max(50.0f, size.y - statusHeight);
        size.x = std::max(50.0f, size.x);
        canvasSize = size;

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 canvasMax = ImVec2(origin.x + size.x, origin.y + size.y);

        ImGui::InvisibleButton("##Canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        const ImGuiIO& io = ImGui::GetIO();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(origin, canvasMax, true);
        drawList->AddRectFilled(origin, canvasMax, IM_COL32(22, 22, 24, 255));

        hoverValid = false;

        const bool ready = texture && texture->IsValid() && texture->GetWidth() > 0 && texture->GetHeight() > 0;
        if (!ready)
        {
            const char* message = "No preview";
            const ImVec2 textSize = ImGui::CalcTextSize(message);
            drawList->AddText(ImVec2(origin.x + (size.x - textSize.x) * 0.5f, origin.y + (size.y - textSize.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), message);
            drawList->PopClipRect();
            return;
        }

        const float width = float(texture->GetWidth());
        const float height = float(texture->GetHeight());
        const float extent = tiled ? 3.0f : 1.0f;

        if (fitPending)
        {
            zoom = std::clamp(std::min(size.x / (width * extent), size.y / (height * extent)) * 0.95f, kMinZoom, 8.0f);
            pan = ImVec2(0.0f, 0.0f);
            fitPending = false;
        }

        const ImVec2 canvasCenter = ImVec2(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);

        if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)))
        {
            pan.x += io.MouseDelta.x;
            pan.y += io.MouseDelta.y;
        }

        if (hovered && io.MouseWheel != 0.0f)
        {
            const float newZoom = std::clamp(zoom * std::pow(1.15f, io.MouseWheel), kMinZoom, kMaxZoom);
            const float ratio = newZoom / zoom;
            const ImVec2 center = ImVec2(canvasCenter.x + pan.x, canvasCenter.y + pan.y);
            const ImVec2 offset = ImVec2(io.MousePos.x - center.x, io.MousePos.y - center.y);
            pan.x = io.MousePos.x - offset.x * ratio - canvasCenter.x;
            pan.y = io.MousePos.y - offset.y * ratio - canvasCenter.y;
            zoom = newZoom;
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            FitToView();

        const ImVec2 tileSize = ImVec2(width * zoom, height * zoom);
        const ImVec2 center = ImVec2(canvasCenter.x + pan.x, canvasCenter.y + pan.y);
        const ImVec2 p0 = ImVec2(center.x - tileSize.x * 0.5f, center.y - tileSize.y * 0.5f);
        const ImVec2 p1 = ImVec2(p0.x + tileSize.x, p0.y + tileSize.y);

        const ImVec2 areaMin = tiled ? ImVec2(p0.x - tileSize.x, p0.y - tileSize.y) : p0;
        const ImVec2 areaMax = tiled ? ImVec2(p1.x + tileSize.x, p1.y + tileSize.y) : p1;

        if (backdrop == Backdrop::Checker)
        {
            const float cell = 10.0f;
            const ImVec2 visibleMin = ImVec2(std::max(areaMin.x, origin.x), std::max(areaMin.y, origin.y));
            const ImVec2 visibleMax = ImVec2(std::min(areaMax.x, canvasMax.x), std::min(areaMax.y, canvasMax.y));

            if (visibleMax.x > visibleMin.x && visibleMax.y > visibleMin.y)
            {
                drawList->AddRectFilled(visibleMin, visibleMax, IM_COL32(70, 70, 74, 255));

                const int firstX = int(std::floor((visibleMin.x - areaMin.x) / cell));
                const int firstY = int(std::floor((visibleMin.y - areaMin.y) / cell));
                const int lastX = int(std::ceil((visibleMax.x - areaMin.x) / cell));
                const int lastY = int(std::ceil((visibleMax.y - areaMin.y) / cell));

                for (int cy = firstY; cy < lastY; cy++)
                {
                    for (int cx = firstX; cx < lastX; cx++)
                    {
                        if (((cx + cy) & 1) == 0)
                            continue;

                        const ImVec2 a = ImVec2(std::max(areaMin.x + cx * cell, visibleMin.x), std::max(areaMin.y + cy * cell, visibleMin.y));
                        const ImVec2 b = ImVec2(std::min(areaMin.x + (cx + 1) * cell, visibleMax.x), std::min(areaMin.y + (cy + 1) * cell, visibleMax.y));
                        drawList->AddRectFilled(a, b, IM_COL32(100, 100, 106, 255));
                    }
                }
            }
        }
        else
        {
            const ImU32 color = backdrop == Backdrop::Black ? IM_COL32(0, 0, 0, 255) : backdrop == Backdrop::Grey ? IM_COL32(128, 128, 128, 255) : IM_COL32(255, 255, 255, 255);
            drawList->AddRectFilled(areaMin, areaMax, color);
        }

        const ImU32 tint = IM_COL32(showRed ? 255 : 0, showGreen ? 255 : 0, showBlue ? 255 : 0, 255);
        const void* textureId = reinterpret_cast<void*>(static_cast<intptr_t>(texture->GetHandle()));

        for (int dy = tiled ? -1 : 0; dy <= (tiled ? 1 : 0); dy++)
        {
            for (int dx = tiled ? -1 : 0; dx <= (tiled ? 1 : 0); dx++)
            {
                const ImVec2 a = ImVec2(p0.x + dx * tileSize.x, p0.y + dy * tileSize.y);
                const ImVec2 b = ImVec2(a.x + tileSize.x, a.y + tileSize.y);
                if (b.x < origin.x || a.x > canvasMax.x || b.y < origin.y || a.y > canvasMax.y)
                    continue;
                drawList->AddImage(const_cast<void*>(textureId), a, b, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
            }
        }

        if (showGrid && zoom >= 6.0f)
        {
            const ImU32 gridColor = IM_COL32(255, 255, 255, 40);
            const int firstX = std::max(0, int(std::floor((origin.x - p0.x) / zoom)));
            const int lastX = std::min(int(width), int(std::ceil((canvasMax.x - p0.x) / zoom)));
            const int firstY = std::max(0, int(std::floor((origin.y - p0.y) / zoom)));
            const int lastY = std::min(int(height), int(std::ceil((canvasMax.y - p0.y) / zoom)));

            for (int x = firstX; x <= lastX; x++)
                drawList->AddLine(ImVec2(p0.x + x * zoom, p0.y), ImVec2(p0.x + x * zoom, p1.y), gridColor);
            for (int y = firstY; y <= lastY; y++)
                drawList->AddLine(ImVec2(p0.x, p0.y + y * zoom), ImVec2(p1.x, p0.y + y * zoom), gridColor);
        }

        drawList->AddRect(p0, p1, IM_COL32(255, 255, 255, tiled ? 120 : 60));

        if (hovered)
        {
            float u = (io.MousePos.x - p0.x) / tileSize.x;
            float v = (io.MousePos.y - p0.y) / tileSize.y;

            if (tiled)
            {
                const bool insideArea = io.MousePos.x >= areaMin.x && io.MousePos.x <= areaMax.x && io.MousePos.y >= areaMin.y && io.MousePos.y <= areaMax.y;
                u -= std::floor(u);
                v -= std::floor(v);
                hoverValid = insideArea;
            }
            else
                hoverValid = u >= 0.0f && u < 1.0f && v >= 0.0f && v < 1.0f;

            hoverU = u;
            hoverV = v;
        }

        drawList->PopClipRect();
    }

    void TextureEditorPanel::DrawStatusLine()
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%.0f%%", zoom * 100.0f);

        if (!hoverValid)
            return;

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        unsigned char texel[4];
        int x = 0, y = 0;
        if (SampleSource(hoverU, hoverV, texel, x, y))
        {
            ImGui::Text("(%d, %d)  R %d  G %d  B %d  A %d  #%02X%02X%02X%02X", x, y, texel[0], texel[1], texel[2], texel[3], texel[0], texel[1], texel[2], texel[3]);
            ImGui::SameLine();
            ImGui::ColorButton("##Texel", ImVec4(texel[0] / 255.0f, texel[1] / 255.0f, texel[2] / 255.0f, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha, ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()));
        }
        else
            ImGui::Text("UV (%.3f, %.3f)", hoverU, hoverV);
    }

    void TextureEditorPanel::DrawInformation()
    {
        ImGui::SeparatorText("Information");

        if (!BeginRows("##TextureInfo"))
            return;

        uint32_t width = 0, height = 0;
        ResolvedSize(width, height);

        Row("File");
        ImGui::TextUnformatted(path.GetFilename().c_str());

        Row("Source");
        if (source.valid)
            ImGui::Text("%u x %u, %u channel%s", source.width, source.height, source.channels, source.channels > 1 ? "s" : "");
        else
            ImGui::TextDisabled("unreadable");

        Row("File size");
        ImGui::TextUnformatted(FormatBytes(uint64_t(std::max(0, fileSize))).c_str());

        if (source.valid)
        {
            Row("Result");
            const bool pow2 = Rendering::IsPowerOfTwo(width) && Rendering::IsPowerOfTwo(height);
            ImGui::Text("%u x %u", width, height);
            ImGui::SameLine();
            if (pow2)
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1.0f), "power of 2");
            else
                ImGui::TextDisabled("not a power of 2");

            Row("Mip levels");
            ImGui::Text("%u", edited.generateMips ? Rendering::MipCount(width, height) : 1u);

            Row("Video memory");
            const uint64_t estimate = Rendering::EstimateTextureBytes(edited, source.width, source.height, source.channels);
            ImGui::Text("~%s", FormatBytes(estimate).c_str());
            if (IsDirty())
            {
                const uint64_t current = Rendering::EstimateTextureBytes(saved, source.width, source.height, source.channels);
                if (current != estimate)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(was ~%s)", FormatBytes(current).c_str());
                }
            }
        }

        ImGui::EndTable();
    }

    void TextureEditorPanel::DrawSizeSettings()
    {
        ImGui::SeparatorText("Size");

        if (!BeginRows("##TextureSize"))
            return;

        Row("Resize");
        EnumCombo("##SizeMode", edited.sizeMode, kSizeModeValues, kSizeModeNames);

        if (edited.sizeMode == TextureSizeMode::Custom)
        {
            if (edited.customWidth == 0) edited.customWidth = std::max(1u, source.width);
            if (edited.customHeight == 0) edited.customHeight = std::max(1u, source.height);

            const double ratio = source.valid && source.height > 0 ? double(source.width) / double(source.height) : 1.0;

            int newWidth = int(edited.customWidth);
            int newHeight = int(edited.customHeight);

            Row("Width");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputInt("##CustomWidth", &newWidth, 0, 0))
            {
                newWidth = std::clamp(newWidth, 1, int(kMaxSide));
                edited.customWidth = uint32_t(newWidth);
                if (keepAspect)
                    edited.customHeight = std::clamp(uint32_t(std::lround(newWidth / ratio)), 1u, kMaxSide);
            }

            Row("Height");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputInt("##CustomHeight", &newHeight, 0, 0))
            {
                newHeight = std::clamp(newHeight, 1, int(kMaxSide));
                edited.customHeight = uint32_t(newHeight);
                if (keepAspect)
                    edited.customWidth = std::clamp(uint32_t(std::lround(newHeight * ratio)), 1u, kMaxSide);
            }

            Row("");
            ImGui::Checkbox("Keep aspect ratio", &keepAspect);
        }

        Row("Snap");
        {
            const bool custom = edited.sizeMode == TextureSizeMode::Custom;

            auto snap = [&](const char* label, TextureSizeMode mode, uint32_t(*round)(uint32_t), const char* tip){
                if (ImGui::SmallButton(label))
                {
                    if (custom)
                    {
                        edited.customWidth = round(std::max(1u, edited.customWidth));
                        edited.customHeight = round(std::max(1u, edited.customHeight));
                    }
                    else
                        edited.sizeMode = mode;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", tip);
            };

            snap("Down", TextureSizeMode::PreviousPowerOfTwo, Rendering::PreviousPowerOfTwo, "Previous power of 2");
            ImGui::SameLine();
            snap("Nearest", TextureSizeMode::NearestPowerOfTwo, Rendering::NearestPowerOfTwo, "Nearest power of 2");
            ImGui::SameLine();
            snap("Up", TextureSizeMode::NextPowerOfTwo, Rendering::NextPowerOfTwo, "Next power of 2");
            ImGui::SameLine();
            if (ImGui::SmallButton("Source"))
            {
                if (custom)
                {
                    edited.customWidth = std::max(1u, source.width);
                    edited.customHeight = std::max(1u, source.height);
                }
                else
                    edited.sizeMode = TextureSizeMode::Original;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Back to the size of the file");
        }

        Row("Max size");
        {
            int current = -1;
            for (int i = 0; i < int(IM_ARRAYSIZE(kMaxSizes)); i++)
                if (kMaxSizes[i] == edited.maxSize) current = i;

            char preview[24];
            if (edited.maxSize == 0)
                std::snprintf(preview, sizeof(preview), "No limit");
            else
                std::snprintf(preview, sizeof(preview), "%u", edited.maxSize);

            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##MaxSize", preview))
            {
                for (int i = 0; i < int(IM_ARRAYSIZE(kMaxSizes)); i++)
                {
                    char label[24];
                    if (kMaxSizes[i] == 0)
                        std::snprintf(label, sizeof(label), "No limit");
                    else
                        std::snprintf(label, sizeof(label), "%u", kMaxSizes[i]);

                    if (ImGui::Selectable(label, i == current))
                        edited.maxSize = kMaxSizes[i];
                }
                ImGui::EndCombo();
            }
        }

        ImGui::EndTable();
    }

    void TextureEditorPanel::DrawSamplingSettings()
    {
        ImGui::SeparatorText("Sampling");

        if (!BeginRows("##TextureSampling"))
            return;

        Row("Mipmaps");
        ImGui::Checkbox("##Mips", &edited.generateMips);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Smaller copies of the texture, used when it is far or at a grazing angle.");

        Row("Compression");
        ImGui::Checkbox("##Compress", &edited.compress);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Block compression (BC7, BC5, BC4) : a quarter to an eighth of the video memory, with a small loss of quality. Only applied from 128 x 128.");

        Row("Min filter");
        EnumCombo("##MinFilter", edited.minFilter, kMinFilterValues, kMinFilterNames);

        Row("Mag filter");
        EnumCombo("##MagFilter", edited.magFilter, kMagFilterValues, kMagFilterNames);

        Row("Wrap U");
        EnumCombo("##WrapS", edited.wrapS, kWrapValues, kWrapNames);

        Row("Wrap V");
        EnumCombo("##WrapT", edited.wrapT, kWrapValues, kWrapNames);

        ImGui::EndTable();

        if (!edited.generateMips && (edited.minFilter == TextureFilter::LinearMipmapLinear || edited.minFilter == TextureFilter::LinearMipmapNearest
            || edited.minFilter == TextureFilter::NearestMipmapLinear || edited.minFilter == TextureFilter::NearestMipmapNearest))
            ImGui::TextDisabled("Without mipmaps, the min filter falls back to its non mipmapped version.");
    }

    void TextureEditorPanel::DrawActions()
    {
        ImGui::Spacing();
        ImGui::Separator();

        const bool dirty = IsDirty();

        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button("Apply"))
            Save();
        ImGui::SameLine();
        if (ImGui::Button("Revert"))
            Revert();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(edited == TextureImportSettings());
        if (ImGui::Button("Defaults"))
            edited = TextureImportSettings();
        ImGui::EndDisabled();

        if (dirty)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(unsaved)");
        }

        ImGui::TextDisabled("Stored in %s%s", path.GetFilename().c_str(), Rendering::kTextureSettingsExtension);
    }
}
