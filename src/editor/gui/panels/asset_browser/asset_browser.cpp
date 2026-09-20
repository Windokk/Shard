#include "asset_browser.hpp"

#include "editor/gui/main_window.hpp"

#include "engine/projects/project.hpp"
#include "engine/levels/level_manager.hpp"
#include "engine/levels/level.hpp"
#include "engine/core/resources/resources_manager.hpp"
#include "engine/rendering/renderer/renderer.hpp"
#include "engine/rendering/texture/texture.hpp"

#include "editor/gui/resources/mesh_thumbnail_cache.hpp"
#include "editor/gui/panels/asset_editor_registry.hpp"
#include "editor/gui/dragdrop/asset_drag_drop.hpp"
#include "editor/gui/popups.hpp"
#include "editor/gui/notifications.hpp"
#include "editor/gui/panels/asset_browser/asset_operations.hpp"

#include <cstdio>
#include <functional>
#include <algorithm>
#include <filesystem>

namespace Shard::Editor::GUI{

    const ImGuiTableSortSpecs* Asset::s_current_sort_specs = NULL;

    // Unreal-esque per-type color coding, used both for the tile's accent bar and its tooltip.
    static ImU32 GetTypeAccentColor(Engine::Filesystem::Type type, bool isDirectory)
    {
        if (isDirectory)
            return IM_COL32(226, 188, 116, 255); // folder tan

        switch (type)
        {
            case Engine::Filesystem::Type::T_MODEL:          return IM_COL32(230, 126, 34, 255);  // orange
            case Engine::Filesystem::Type::T_MATERIAL:       return IM_COL32(46, 204, 113, 255);  // green
            case Engine::Filesystem::Type::T_IMAGE:          return IM_COL32(175, 110, 220, 255); // purple/pink
            case Engine::Filesystem::Type::T_SOUND:          return IM_COL32(26, 188, 156, 255);  // teal
            case Engine::Filesystem::Type::T_SCRIPT:         return IM_COL32(241, 196, 15, 255);  // yellow
            case Engine::Filesystem::Type::T_LEVEL:          return IM_COL32(231, 76, 60, 255);   // red
            case Engine::Filesystem::Type::T_SHADER:         return IM_COL32(52, 152, 219, 255);  // blue
            case Engine::Filesystem::Type::T_COMPUTE_SHADER: return IM_COL32(41, 128, 185, 255);  // dark blue
            case Engine::Filesystem::Type::T_FONT:           return IM_COL32(149, 165, 166, 255); // grey-blue
            case Engine::Filesystem::Type::T_CONFIG:         return IM_COL32(127, 140, 141, 255); // grey
            case Engine::Filesystem::Type::T_TEXT:           return IM_COL32(189, 195, 199, 255); // light grey
            default:                                         return IM_COL32(150, 150, 150, 255);
        }
    }

    static const char* GetTypeDisplayName(Engine::Filesystem::Type type, bool isDirectory)
    {
        if (isDirectory)
            return "Folder";

        switch (type)
        {
            case Engine::Filesystem::Type::T_MODEL:          return "Static Mesh";
            case Engine::Filesystem::Type::T_MATERIAL:       return "Material";
            case Engine::Filesystem::Type::T_IMAGE:          return "Texture";
            case Engine::Filesystem::Type::T_SOUND:          return "Sound";
            case Engine::Filesystem::Type::T_SCRIPT:         return "Script";
            case Engine::Filesystem::Type::T_LEVEL:          return "Level";
            case Engine::Filesystem::Type::T_SHADER:         return "Shader";
            case Engine::Filesystem::Type::T_COMPUTE_SHADER: return "Compute Shader";
            case Engine::Filesystem::Type::T_FONT:           return "Font";
            case Engine::Filesystem::Type::T_CONFIG:         return "Config";
            case Engine::Filesystem::Type::T_TEXT:           return "Text File";
            default:                                         return "Asset";
        }
    }

    // Wraps a filename into at most two centered lines that fit within maxWidth, truncating
    // the second line with an ellipsis if the name is still too long to fully display.
    static void WrapLabelToLines(const std::string& text, float maxWidth, std::string& line1, std::string& line2)
    {
        line2.clear();

        if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
        {
            line1 = text;
            return;
        }

        ImFont* font = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize();

        const char* text_begin = text.c_str();
        const char* text_end = text_begin + text.size();

        const char* line1_end = font->CalcWordWrapPosition(fontSize, text_begin, text_end, maxWidth);
        if (line1_end <= text_begin)
            line1_end = text_begin + 1;

        line1.assign(text_begin, line1_end);

        const char* remaining_begin = line1_end;
        while (remaining_begin < text_end && *remaining_begin == ' ')
            remaining_begin++;

        if (remaining_begin >= text_end)
            return;

        std::string remaining(remaining_begin, text_end);

        if (ImGui::CalcTextSize(remaining.c_str()).x <= maxWidth)
        {
            line2 = remaining;
            return;
        }

        const char* line2_end = font->CalcWordWrapPosition(fontSize, remaining_begin, text_end, maxWidth);
        if (line2_end <= remaining_begin)
            line2_end = remaining_begin + 1;

        std::string truncated(remaining_begin, line2_end);
        while (!truncated.empty() && ImGui::CalcTextSize((truncated + "...").c_str()).x > maxWidth)
            truncated.pop_back();

        line2 = truncated + "...";
    }

    static std::string FormatFileSize(int bytes)
    {
        if (bytes < 0)
            return "";

        char buf[32];

        if (bytes < 1024)
        {
            snprintf(buf, sizeof(buf), "%d B", bytes);
            return buf;
        }

        double kb = bytes / 1024.0;
        if (kb < 1024.0)
        {
            snprintf(buf, sizeof(buf), "%.1f KB", kb);
            return buf;
        }

        snprintf(buf, sizeof(buf), "%.1f MB", kb / 1024.0);
        return buf;
    }

    void AssetBrowser::Refresh()
    {
        items.clear();

        auto* fileManager = Engine::Core::GetEngine().GetFileManager();

        auto files = fileManager->ListDirectory(
            currentPath,
            allowedTypes,
            true,
            false
        );

        auto atlas = EditorResources::Instance().GetIconAtlas();

        for (int i = 0; i < files.size(); i++)
        {
            auto file = files[i];

            items.push_back(Asset(
                ImHashStr(file.path.full.c_str()),
                file.path,
                file.type,
                file.isDirectory,
                &atlas->GetRegion(file.isDirectory ? Engine::Filesystem::Type::T_DIRECTORY : file.type),
                file.nameInProject,
                file.size
            ));
        }

        dirty = false;
    }

    void AssetBrowser::Draw()
    {
        if (dirty)
            Refresh();

        MeshThumbnailCache::Instance().BeginFrame();

        ImGui::Begin("Asset Browser");

        DrawBreadcrumb();
        ImGui::Separator();

        DrawAssets();

        ProcessAction();
        DrawDialogs();

        ImGui::End();
    }

    void AssetBrowser::DrawBreadcrumb()
    {
        auto& engine = Engine::Core::GetEngine();

        Engine::Filesystem::Path projectRoot = engine.GetCurrentProject()->GetProjectResourcesPath();

        // currentPath relative to projectRoot (NOT the other way around - that would compute the
        // path FROM currentPath back up TO the root, i.e. "..", which is backwards for a breadcrumb).
        std::string relative = currentPath.RelativeTo(projectRoot).full;
        if (relative == ".")
            relative.clear();

        std::vector<std::string> segments;
        if (!relative.empty())
        {
            std::stringstream ss(relative);
            std::string segment;
            while (std::getline(ss, segment, '/'))
                if (!segment.empty())
                    segments.push_back(segment);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.14f));

        auto drawSeparator = [&]()
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.30f), ">");
            ImGui::SameLine();
        };

        auto drawCrumb = [&](int id, const std::string& label, bool isCurrent, const std::function<void()>& onClick)
        {
            ImGui::PushID(id);

            if (isCurrent)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                ImGui::BeginDisabled();
                ImGui::Button(label.c_str());
                ImGui::EndDisabled();
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.65f));
                if (ImGui::Button(label.c_str()))
                    onClick();
                ImGui::PopStyleColor();
            }

            ImGui::PopID();
        };

        drawCrumb(0, engine.GetCurrentProject()->GetProjectResourcesPath().GetFilename(),
            segments.empty(),
            [&]() { NavigateTo(projectRoot.full); });

        Engine::Filesystem::Path accum = projectRoot;

        for (size_t i = 0; i < segments.size(); i++)
        {
            drawSeparator();

            accum = accum / segments[i];
            bool isCurrent = (i == segments.size() - 1);

            drawCrumb((int)i + 1, segments[i], isCurrent, [&, accum]() { NavigateTo(accum.full); });
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);
    }

    void AssetBrowser::DrawAssets()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowContentSize(ImVec2(0.0f, LayoutOuterPadding + LayoutLineCount * (LayoutItemSize.y + LayoutItemSpacing)));
        if (ImGui::BeginChild("Assets", ImVec2(0.0f, -ImGui::GetTextLineHeightWithSpacing()), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoMove))
        {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            const float avail_width = ImGui::GetContentRegionAvail().x;
            UpdateLayoutSizes(avail_width);

            // Calculate and store start position.
            ImVec2 start_pos = ImGui::GetCursorScreenPos();
            start_pos = ImVec2(start_pos.x + LayoutOuterPadding, start_pos.y + LayoutOuterPadding);
            ImGui::SetCursorScreenPos(start_pos);

            // Multi-select
            ImGuiMultiSelectFlags ms_flags = ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_ClearOnClickVoid;

            // - Enable box-select (in 2D mode, so that changing box-select rectangle X1/X2 boundaries will affect clipped items)
            if (AllowBoxSelect)
                ms_flags |= ImGuiMultiSelectFlags_BoxSelect2d;

            // - This feature allows dragging an unselected item without selecting it (rarely used)
            if (AllowDragUnselected)
                ms_flags |= ImGuiMultiSelectFlags_SelectOnClickRelease;

            // - Enable keyboard wrapping on X axis
            // (FIXME-MULTISELECT: We haven't designed/exposed a general nav wrapping api yet, so this flag is provided as a courtesy to avoid doing:
            //    ImGui::NavMoveRequestTryWrapping(ImGui::GetCurrentWindow(), ImGuiNavMoveFlags_WrapX);
            // When we finish implementing a more general API for this, we will obsolete this flag in favor of the new system)
            ms_flags |= ImGuiMultiSelectFlags_NavWrapX;

            ImGuiMultiSelectIO* ms_io = ImGui::BeginMultiSelect(ms_flags, selection.Size, items.size());

            // Use custom selection adapter: store ID in selection (recommended)
            selection.UserData = this;
            selection.AdapterIndexToStorageId = [](ImGuiSelectionBasicStorage* self_, int idx) { AssetBrowser* self = (AssetBrowser*)self_->UserData; return self->items[idx].id; };
            selection.ApplyRequests(ms_io);

            // Keyboard shortcuts (this child has focus). They set the same pendingAction the context menu does.
            if (ImGui::Shortcut(ImGuiKey_Delete) && selection.Size > 0)
                pendingAction = Action::Delete;
            else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C) && selection.Size > 0)
                pendingAction = Action::Copy;
            else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_X) && selection.Size > 0)
                pendingAction = Action::Cut;
            else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V) && !clipboard.empty())
                pendingAction = Action::Paste;
            else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D) && selection.Size > 0)
                pendingAction = Action::Duplicate;
            else if (ImGui::Shortcut(ImGuiKey_F2) && selection.Size == 1)
                pendingAction = Action::Rename;
            else if (ImGui::Shortcut(ImGuiKey_F5))
                pendingAction = Action::Refresh;

            const int item_curr_idx_to_focus = -1;

            // Push LayoutSelectableSpacing (which is LayoutItemSpacing minus hit-spacing, if we decide to have hit gaps between items)
            // Altering style ItemSpacing may seem unnecessary as we position every items using SetCursorScreenPos()...
            // But it is necessary for two reasons:
            // - Selectables uses it by default to visually fill the space between two items.
            // - The vertical spacing would be measured by Clipper to calculate line height if we didn't provide it explicitly (here we do).
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(LayoutSelectableSpacing, LayoutSelectableSpacing));

            // Rendering parameters
            const bool display_label = (LayoutItemSize.x >= ImGui::CalcTextSize("999").x);

            const int column_count = LayoutColumnCount;
            ImGuiListClipper clipper;
            clipper.Begin(LayoutLineCount, LayoutItemStep.y);
            if (item_curr_idx_to_focus != -1)
                clipper.IncludeItemByIndex(item_curr_idx_to_focus / column_count); // Ensure focused item line is not clipped.
            if (ms_io->RangeSrcItem != -1)
                clipper.IncludeItemByIndex((int)ms_io->RangeSrcItem / column_count); // Ensure RangeSrc item line is not clipped.
            while (clipper.Step())
            {
                for (int line_idx = clipper.DisplayStart; line_idx < clipper.DisplayEnd; line_idx++)
                {
                    const int item_min_idx_for_current_line = line_idx * column_count;
                    const int item_max_idx_for_current_line = IM_MIN((line_idx + 1) * column_count, items.size());
                    for (int item_idx = item_min_idx_for_current_line; item_idx < item_max_idx_for_current_line; ++item_idx)
                    {
                        Asset* item_data = &items[item_idx];
                        ImGui::PushID((int)item_data->id);

                        // Position item
                        ImVec2 pos = ImVec2(start_pos.x + (item_idx % column_count) * LayoutItemStep.x, start_pos.y + line_idx * LayoutItemStep.y);
                        ImGui::SetCursorScreenPos(pos);

                        ImGui::SetNextItemSelectionUserData(item_idx);
                        bool item_is_selected = selection.Contains((ImGuiID)item_data->id);
                        bool item_is_visible = ImGui::IsRectVisible(LayoutItemSize);
                        ImGui::Selectable("", item_is_selected, ImGuiSelectableFlags_None, LayoutItemSize);

                        bool item_hovered = ImGui::IsItemHovered();

                        // Right-click acts on the clicked item (or the whole selection if it is part of it)
                        if (item_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                        {
                            if (!item_is_selected)
                            {
                                selection.Clear();
                                selection.SetItemSelected(item_data->id, true);
                                item_is_selected = true;
                            }
                            contextTargetId = item_data->id;
                        }

                        if (item_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            OpenItem(*item_data);

                        if (!ImGui::IsDragDropActive() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        {
                            ImGui::BeginTooltip();

                            ImVec4 accentColor = ImGui::ColorConvertU32ToFloat4(GetTypeAccentColor(item_data->type, item_data->isDirectory));

                            ImGui::TextUnformatted(item_data->path.GetFilename().c_str());
                            ImGui::TextColored(accentColor, "%s", GetTypeDisplayName(item_data->type, item_data->isDirectory));

                            if (!item_data->isDirectory)
                            {
                                ImGui::Separator();
                                ImGui::TextDisabled("%s", FormatFileSize(item_data->size).c_str());

                                if (item_data->type == Engine::Filesystem::Type::T_IMAGE)
                                {
                                    auto tex = Engine::Core::GetEngine().GetResourcesManager()->GetTexture(item_data->nameInProject);
                                    if (tex)
                                        ImGui::TextDisabled("%u x %u", tex->GetWidth(), tex->GetHeight());
                                }
                            }

                            ImGui::TextDisabled("%s", item_data->nameInProject.c_str());

                            ImGui::EndTooltip();
                        }

                        // Update our selection state immediately (without waiting for EndMultiSelect() requests)
                        // because we use this to alter the color of our text/icon.
                        if (ImGui::IsItemToggledSelection())
                            item_is_selected = !item_is_selected;

                        // Focus (for after deletion)
                        if (item_curr_idx_to_focus == item_idx)
                            ImGui::SetKeyboardFocusHere(-1);

                        // Drag and drop - the payload carries each dragged item's nameInProject (see
                        // DragDrop::SetAssetDragDropPayload) so any drop target elsewhere in the editor
                        // (viewport, level tree, inspector asset fields, material slots, ...) can feed
                        // it straight into ResourcesManager/AssetIDManager lookups.
                        if (ImGui::BeginDragDropSource())
                        {
                            // Create payload with full selection OR single unselected item.
                            // (the later is only possible when using ImGuiMultiSelectFlags_SelectOnClickRelease)
                            if (ImGui::GetDragDropPayload() == NULL)
                            {
                                std::vector<std::string> dragged;
                                if (!item_is_selected)
                                    dragged.push_back(item_data->nameInProject);
                                else
                                {
                                    void* it = NULL;
                                    ImGuiID id = 0;
                                    while (selection.GetNextSelectedItem(&it, &id))
                                    {
                                        auto found = std::find_if(items.begin(), items.end(),
                                            [id](const Asset& a) { return a.id == id; });
                                        if (found != items.end())
                                            dragged.push_back(found->nameInProject);
                                    }
                                }
                                DragDrop::SetAssetDragDropPayload(dragged);
                            }

                            // Display payload content in tooltip, by extracting it from the payload data
                            // (we could read from selection, but it is more correct and reusable to read from payload)
                            std::vector<std::string> payloadNames = DragDrop::PeekAssetDragDropPayload();
                            if (payloadNames.size() == 1)
                            {
                                Engine::Filesystem::Type t = Engine::Filesystem::Path(payloadNames[0]).GetExtensionType();
                                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(GetTypeAccentColor(t, false)),
                                    "%s", GetTypeDisplayName(t, false));
                            }
                            else
                            {
                                ImGui::Text("%d assets", (int)payloadNames.size());
                            }

                            ImGui::EndDragDropSource();
                        }

                        // Render icon/thumbnail, Unreal-Content-Browser-style: a type-tinted tile,
                        // a real texture preview for images (falling back to the atlas icon for
                        // everything else), a solid color-coded accent bar under the thumbnail, and
                        // a selection/hover outline.
                        if (item_is_visible)
                        {
                            ImVec2 box_min(pos.x - 1, pos.y - 1);
                            ImVec2 box_max(box_min.x + LayoutItemSize.x + 2, box_min.y + LayoutItemSize.y + 2); // Dubious

                            ImU32 accent = GetTypeAccentColor(item_data->type, item_data->isDirectory);
                            ImVec4 accentF = ImGui::ColorConvertU32ToFloat4(accent);

                            // Base tile background subtly tinted towards the type's accent color,
                            // brightened a touch on hover.
                            ImVec4 baseBg = item_hovered ? ImVec4(0.205f, 0.205f, 0.220f, 0.92f) : ImVec4(0.149f, 0.149f, 0.161f, 0.86f);
                            const float tintWeight = 0.10f;
                            ImU32 tileBg = ImGui::ColorConvertFloat4ToU32(ImVec4(
                                baseBg.x * (1.0f - tintWeight) + accentF.x * tintWeight,
                                baseBg.y * (1.0f - tintWeight) + accentF.y * tintWeight,
                                baseBg.z * (1.0f - tintWeight) + accentF.z * tintWeight,
                                baseBg.w
                            ));

                            draw_list->AddRectFilled(box_min, box_max, tileBg, 4.0f);

                            if (item_is_selected)
                                draw_list->AddRect(box_min, box_max, IM_COL32(35, 154, 255, 255), 4.0f, 0, 2.0f);
                            else if (item_hovered)
                                draw_list->AddRect(box_min, box_max, IM_COL32(255, 255, 255, 90), 4.0f, 0, 1.0f);

                            // Icon sits at a fixed distance from the top of the tile (horizontally
                            // centered) rather than centered across the full tile height, so its
                            // position - and the accent bar right under it - stay put regardless of
                            // how tall the label below ends up being (one vs two lines).
                            ImVec2 icon_offset = ImVec2(
                                (LayoutItemSize.x - ThumbnailSize.x) * 0.5f,
                                IconTopPadding
                            );

                            ImVec2 icon_min = ImVec2(
                                box_min.x + icon_offset.x,
                                box_min.y + icon_offset.y
                            );

                            ImVec2 icon_max = ImVec2(
                                icon_min.x + ThumbnailSize.x,
                                icon_min.y + ThumbnailSize.y
                            );

                            // Real preview for images and static meshes; every other type falls
                            // back to its atlas icon. Images sample their own already-loaded
                            // texture directly; meshes are rendered white/unlit into a shared
                            // thumbnail atlas the first time they're seen (see MeshThumbnailCache).
                            std::shared_ptr<Engine::Rendering::Texture2D> imageThumbnail;
                            const AtlasRegion* meshThumbnail = nullptr;

                            if (!item_data->isDirectory && item_data->type == Engine::Filesystem::Type::T_IMAGE)
                                imageThumbnail = Engine::Core::GetEngine().GetResourcesManager()->GetTexture(item_data->nameInProject);
                            else if (!item_data->isDirectory && item_data->type == Engine::Filesystem::Type::T_MODEL)
                                meshThumbnail = MeshThumbnailCache::Instance().GetOrCreateThumbnail(item_data->nameInProject);

                            if (imageThumbnail && imageThumbnail->IsValid())
                            {
                                draw_list->AddRectFilled(icon_min, icon_max, IM_COL32(18, 18, 20, 255));
                                draw_list->AddImage(
                                    (void*)(intptr_t)imageThumbnail->GetHandle(),
                                    icon_min,
                                    icon_max
                                );
                                draw_list->AddRect(icon_min, icon_max, IM_COL32(0, 0, 0, 130));
                            }
                            else if (meshThumbnail)
                            {
                                draw_list->AddImage(
                                    (void*)(intptr_t)MeshThumbnailCache::Instance().GetAtlasTextureHandle(),
                                    icon_min,
                                    icon_max,
                                    meshThumbnail->uv0,
                                    meshThumbnail->uv1
                                );
                                draw_list->AddRect(icon_min, icon_max, IM_COL32(0, 0, 0, 130));
                            }
                            else
                            {
                                draw_list->AddImage(
                                    (void*)(intptr_t)EditorResources::Instance().GetIconAtlas()->GetTexture()->GetHandle(),
                                    icon_min,
                                    icon_max,
                                    item_data->icon->uv0,
                                    item_data->icon->uv1
                                );
                            }

                            // Type-color accent bar, sitting right under the thumbnail - the main
                            // "what kind of asset is this" cue, at a glance, across the whole grid.
                            ImVec2 bar_min(box_min.x + 3, icon_max.y + 4);
                            ImVec2 bar_max(box_max.x - 3, bar_min.y + 3);
                            draw_list->AddRectFilled(bar_min, bar_max, accent, 1.5f);

                            if (display_label)
                            {
                                std::string filename = item_data->path.GetFilename();

                                const float labelMaxWidth = (box_max.x - box_min.x) - 8.0f;
                                std::string line1, line2;
                                WrapLabelToLines(filename, labelMaxWidth, line1, line2);

                                const float fontSize = ImGui::GetFontSize();
                                const bool hasSecondLine = !line2.empty();

                                // The label block is always reserved at its full two-line height,
                                // even for names that only need one line - so the strip, and every
                                // line of text across the whole grid, lines up at the same height
                                // regardless of which tiles happen to need wrapping.
                                float stripTop = box_max.y - (fontSize * 2.0f + 2.0f) - 4.0f;
                                draw_list->AddRectFilled(
                                    ImVec2(box_min.x + 2, stripTop),
                                    ImVec2(box_max.x - 2, box_max.y - 2),
                                    IM_COL32(15, 15, 17, 180),
                                    3.0f
                                );

                                ImU32 label_col = ImGui::GetColorU32(item_is_selected ? ImGuiCol_Text : ImGuiCol_TextDisabled);

                                float line1Width = ImGui::CalcTextSize(line1.c_str()).x;
                                float line1Y = box_max.y - fontSize * 2.0f - 3.0f;
                                ImVec2 line1Pos(box_min.x + (box_max.x - box_min.x) * 0.5f - line1Width / 2, line1Y);
                                draw_list->AddText(line1Pos, label_col, line1.c_str());

                                if (hasSecondLine)
                                {
                                    float line2Width = ImGui::CalcTextSize(line2.c_str()).x;
                                    float line2Y = box_max.y - fontSize - 3.0f;
                                    ImVec2 line2Pos(box_min.x + (box_max.x - box_min.x) * 0.5f - line2Width / 2, line2Y);
                                    draw_list->AddText(line2Pos, label_col, line2.c_str());
                                }
                            }
                        }

                        ImGui::PopID();
                    }
                }
            }
            clipper.End();
            ImGui::PopStyleVar(); // ImGuiStyleVar_ItemSpacing

            // Right-click on empty space : no item target, nothing selected
            if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                contextTargetId = 0;
                selection.Clear();
            }

            if (ImGui::BeginPopupContextWindow())
            {
                DrawContextMenu();
                ImGui::EndPopup();
            }

            ms_io = ImGui::EndMultiSelect();
            selection.ApplyRequests(ms_io);
            //if (want_delete)
                //selection.ApplyDeletionPostLoop(ms_io, items, item_curr_idx_to_focus);

            // Zooming with CTRL+Wheel
            if (ImGui::IsWindowAppearing())
                zoomWheelAccum = 0.0f;
            if (ImGui::IsWindowHovered() && io.MouseWheel != 0.0f && ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsAnyItemActive() == false)
            {
                zoomWheelAccum += io.MouseWheel;
                if (fabsf(zoomWheelAccum) >= 1.0f)
                {
                    // Calculate hovered item index from mouse location
                    // FIXME: Locking aiming on 'hovered_item_idx' (with a cool-down timer) would ensure zoom keeps on it.
                    const float hovered_item_nx = (io.MousePos.x - start_pos.x + LayoutItemSpacing * 0.5f) / LayoutItemStep.x;
                    const float hovered_item_ny = (io.MousePos.y - start_pos.y + LayoutItemSpacing * 0.5f) / LayoutItemStep.y;
                    const int hovered_item_idx = ((int)hovered_item_ny * LayoutColumnCount) + (int)hovered_item_nx;
                    //ImGui::SetTooltip("%f,%f -> item %d", hovered_item_nx, hovered_item_ny, hovered_item_idx); // Move those 4 lines in block above for easy debugging

                    // Zoom
                    thumbnailSize *= powf(1.1f, (float)(int)zoomWheelAccum);
                    thumbnailSize = IM_CLAMP(thumbnailSize, 16.0f, 128.0f);
                    zoomWheelAccum -= (int)zoomWheelAccum;
                    UpdateLayoutSizes(avail_width);

                    // Manipulate scroll to that we will land at the same Y location of currently hovered item.
                    // - Calculate next frame position of item under mouse
                    // - Set new scroll position to be used in next ImGui::BeginChild() call.
                    float hovered_item_rel_pos_y = ((float)(hovered_item_idx / LayoutColumnCount) + fmodf(hovered_item_ny, 1.0f)) * LayoutItemStep.y;
                    hovered_item_rel_pos_y += ImGui::GetStyle().WindowPadding.y;
                    float mouse_local_y = io.MousePos.y - ImGui::GetWindowPos().y;
                    ImGui::SetScrollY(hovered_item_rel_pos_y - mouse_local_y);
                }
            }
        }
        ImGui::EndChild();
    }
    
    void AssetBrowser::UpdateLayoutSizes(float avail_width)
    {
        LayoutItemSize = ItemSize;

        // Number of columns
        LayoutColumnCount = IM_MAX(
            (int)(avail_width / (LayoutItemSize.x + Spacing.x)),
            1
        );

        LayoutLineCount = (items.size() + LayoutColumnCount - 1) / LayoutColumnCount;

        LayoutItemStep = ImVec2(
            LayoutItemSize.x + Spacing.x,
            LayoutItemSize.y + Spacing.y
        );

        LayoutItemSpacing = Spacing.x;
        LayoutSelectableSpacing = IM_MAX(Spacing.x - IconHitSpacing, 0.0f);
        LayoutOuterPadding = Spacing.x * 0.5f;
    }

    void AssetBrowser::RequestOpenLevel(const Engine::Filesystem::Path &path)
    {
        auto& engine = Engine::Core::GetEngine();
        auto* levelManager = engine.GetLevelManager();

        if (levelManager->IsAsyncLoadInProgress())
            return;

        // Whatever's currently loaded is only torn down once the new level has resolved
        // successfully (see LevelManager::FinishAsyncLoad) - so a bad/missing target here can't
        // leave the editor with zero levels loaded.
        std::string pathInProject = engine.GetFileManager()->GetFileInfos(path).nameInProject;

        auto* current = levelManager->GetLevelAt(0);
        if (current && current->IsDirty())
        {
            GUI::Popups::ConfirmUnsavedChanges(current->GetName(),
                [levelManager, current, pathInProject]()
                {
                    current->Serialize(current->GetPath());
                    levelManager->LoadLevelAsync(pathInProject);
                },
                [levelManager, pathInProject]()
                {
                    levelManager->LoadLevelAsync(pathInProject);
                });
            return;
        }

        levelManager->LoadLevelAsync(pathInProject);
    }

    void AssetBrowser::SetParentWindow(Core::EditorMainWindow* parent)
    {
        this->parent = parent;
    }

    void AssetBrowser::OpenItem(const Asset& item)
    {
        if (item.isDirectory)
            NavigateTo(item.path.full);
        else if (item.type == Engine::Filesystem::Type::T_LEVEL)
            RequestOpenLevel(item.path);
        else
            AssetEditorRegistry::Instance().TryOpen(item.type, item.path);
    }

    std::vector<const Asset*> AssetBrowser::GetSelectedItems() const
    {
        std::vector<const Asset*> selected;
        for (const Asset& item : items)
        {
            if (selection.Contains(item.id))
                selected.push_back(&item);
        }
        return selected;
    }

    const Asset* AssetBrowser::FindItem(ImGuiID id) const
    {
        for (const Asset& item : items)
        {
            if (item.id == id)
                return &item;
        }
        return nullptr;
    }

    void AssetBrowser::DrawContextMenu()
    {
        const Asset* target = contextTargetId != 0 ? FindItem(contextTargetId) : nullptr;
        const bool onItem = target != nullptr && selection.Size > 0;
        const bool single = selection.Size == 1;
        const bool canPaste = !clipboard.empty();

        if (onItem)
        {
            if (single && ImGui::MenuItem("Open"))
                pendingAction = Action::Open;
            if (ImGui::MenuItem("Show in Explorer"))
                pendingAction = Action::Reveal;

            ImGui::Separator();

            if (ImGui::MenuItem("Cut", "Ctrl+X"))
                pendingAction = Action::Cut;
            if (ImGui::MenuItem("Copy", "Ctrl+C"))
                pendingAction = Action::Copy;
            // Pasting onto a folder drops the clipboard *into* it
            if (ImGui::MenuItem(single && target->isDirectory ? "Paste Into Folder" : "Paste", "Ctrl+V", false, canPaste))
            {
                pendingAction = Action::Paste;
                pasteTargetDir = single && target->isDirectory ? target->path : Engine::Filesystem::Path("");
            }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                pendingAction = Action::Duplicate;

            ImGui::Separator();

            if (ImGui::MenuItem("Rename", "F2", false, single))
                pendingAction = Action::Rename;
            if (ImGui::MenuItem("Delete", "Del"))
                pendingAction = Action::Delete;

            ImGui::Separator();

            if (ImGui::MenuItem("Copy Path", nullptr, false, single))
                pendingAction = Action::CopyPath;
        }
        else
        {
            if (ImGui::BeginMenu("Create"))
            {
                if (ImGui::MenuItem("Folder"))
                    pendingAction = Action::NewFolder;
                if (ImGui::MenuItem("Material"))
                    pendingAction = Action::NewMaterial;
                if (ImGui::MenuItem("Level"))
                    pendingAction = Action::NewLevel;
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste))
            {
                pendingAction = Action::Paste;
                pasteTargetDir = Engine::Filesystem::Path("");
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Show in Explorer"))
                pendingAction = Action::Reveal;
        }

        if (ImGui::MenuItem("Refresh", "F5"))
            pendingAction = Action::Refresh;
    }

    void AssetBrowser::ProcessAction()
    {
        const Action action = pendingAction;
        pendingAction = Action::None;

        if (action == Action::None)
            return;

        namespace Ops = AssetOperations;
        using Engine::Filesystem::Path;
        namespace fs = std::filesystem;

        std::vector<const Asset*> selected = GetSelectedItems();
        std::vector<std::string> failures;

        // name/extension split for making a sibling path : a folder's "extension" is part of its name
        auto splitName = [](const Path& p, std::string& stem, std::string& extension)
        {
            fs::path fsPath(p.full);
            if (fs::is_directory(fsPath))
            {
                stem = fsPath.filename().string();
                extension.clear();
            }
            else
            {
                stem = fsPath.stem().string();
                extension = fsPath.extension().string();
            }
        };

        bool changed = false;

        switch (action)
        {
            case Action::Open:
                if (selected.size() == 1)
                    OpenItem(*selected[0]);
                break;

            case Action::Cut:
            case Action::Copy:
                clipboard.clear();
                for (const Asset* item : selected)
                    clipboard.push_back(item->path);
                clipboardCut = action == Action::Cut;
                break;

            case Action::Paste:
            {
                Path destination = pasteTargetDir.full.empty() ? currentPath : pasteTargetDir;
                pasteTargetDir = Path("");

                for (const Path& source : clipboard)
                {
                    std::string stem, extension;
                    splitName(source, stem, extension);

                    if (clipboardCut)
                    {
                        // Cutting into the folder it already is in is a no-op
                        if (fs::equivalent(fs::path(source.full).parent_path(), fs::path(destination.full)))
                            continue;

                        Ops::Result r = Ops::Move(source, Ops::UniquePath(destination, stem, extension));
                        if (!r.ok) failures.push_back(r.message);
                    }
                    else
                    {
                        Ops::Result r = Ops::Copy(source, Ops::UniquePath(destination, stem, extension));
                        if (!r.ok) failures.push_back(r.message);
                    }
                }

                if (clipboardCut)
                    clipboard.clear();
                changed = true;
                break;
            }

            case Action::Duplicate:
                for (const Asset* item : selected)
                {
                    std::string stem, extension;
                    splitName(item->path, stem, extension);

                    Ops::Result r = Ops::Copy(item->path, Ops::UniquePath(Path(fs::path(item->path.full).parent_path().string()), stem, extension, true));
                    if (!r.ok) failures.push_back(r.message);
                }
                changed = true;
                break;

            case Action::Rename:
                if (selected.size() == 1)
                    BeginRename(selected[0]->path);
                break;

            case Action::Delete:
            {
                std::vector<Path> paths;
                for (const Asset* item : selected)
                    paths.push_back(item->path);
                if (!paths.empty())
                    ConfirmDelete(paths);
                break;
            }

            case Action::Refresh:
            {
                std::string summary = Ops::Sync(currentPath);
                if (!summary.empty())
                    Notifications::Info("Asset browser : %s", summary.c_str());
                changed = true;
                break;
            }

            case Action::NewFolder:
            case Action::NewMaterial:
            case Action::NewLevel:
            {
                Path created;
                Ops::Result r = action == Action::NewFolder ? Ops::CreateFolder(currentPath, created)
                              : action == Action::NewMaterial ? Ops::CreateMaterial(currentPath, created)
                              : Ops::CreateLevel(currentPath, created);
                if (!r.ok)
                    failures.push_back(r.message);
                else
                    BeginRename(created); // name it right away, like every content browser does
                changed = true;
                break;
            }

            case Action::Reveal:
                if (!selected.empty())
                    Ops::RevealInFileExplorer(selected[0]->path.full, true);
                else
                    Ops::RevealInFileExplorer(currentPath.full, false);
                break;

            case Action::CopyPath:
                if (selected.size() == 1)
                    ImGui::SetClipboardText(selected[0]->nameInProject.c_str());
                break;

            default:
                break;
        }

        if (changed)
        {
            selection.Clear();
            dirty = true;
        }

        ReportFailures("Asset Browser", failures);
    }

    void AssetBrowser::ReportFailures(const std::string& title, const std::vector<std::string>& failures)
    {
        if (failures.empty())
            return;

        std::string message;
        for (const std::string& failure : failures)
            message += (message.empty() ? "" : "\n\n") + failure;

        Popups::Show(Popups::PopupType::Error, title, message);
    }

    void AssetBrowser::ConfirmDelete(const std::vector<Engine::Filesystem::Path>& paths)
    {
        namespace fs = std::filesystem;

        std::string message = paths.size() == 1
            ? "Delete \"" + fs::path(paths[0].full).filename().string() + "\" ?"
            : "Delete " + std::to_string(paths.size()) + " items ?";

        // What would be left pointing at a missing asset, as far as the asset database knows
        std::vector<std::string> referencedBy;
        for (const auto& path : paths)
        {
            for (const std::string& name : AssetOperations::ReferencedBy(path))
            {
                if (std::find(referencedBy.begin(), referencedBy.end(), name) == referencedBy.end())
                    referencedBy.push_back(name);
            }
        }

        if (!referencedBy.empty())
        {
            message += "\n\nStill referenced by :";
            for (size_t i = 0; i < referencedBy.size() && i < 6; i++)
                message += "\n  - " + referencedBy[i];
            if (referencedBy.size() > 6)
                message += "\n  ... and " + std::to_string(referencedBy.size() - 6) + " more";
        }

        message += "\n\nThis cannot be undone.";

        Popups::Show(Popups::PopupType::Warning, "Delete", message, {
            { "Delete", [this, paths]()
                {
                    std::vector<std::string> failures;
                    for (const auto& path : paths)
                    {
                        AssetOperations::Result r = AssetOperations::Delete(path);
                        if (!r.ok) failures.push_back(r.message);
                    }
                    selection.Clear();
                    dirty = true;
                    ReportFailures("Delete", failures);
                } },
            { "Cancel", nullptr }
        });
    }

    void AssetBrowser::BeginRename(const Engine::Filesystem::Path& path)
    {
        namespace fs = std::filesystem;

        fs::path fsPath(path.full);
        std::string name = fs::is_directory(fsPath) ? fsPath.filename().string() : fsPath.stem().string();

        renamePath = path;
        snprintf(renameBuffer, sizeof(renameBuffer), "%s", name.c_str());
        openRenamePopup = true;
    }

    void AssetBrowser::ApplyRename()
    {
        namespace fs = std::filesystem;

        fs::path oldPath(renamePath.full);

        std::string name = renameBuffer;
        // Trim
        name.erase(0, name.find_first_not_of(' '));
        name.erase(name.find_last_not_of(' ') + 1);

        if (name.empty() || name.find_first_of("\\/:*?\"<>|") != std::string::npos)
        {
            Popups::Show(Popups::PopupType::Error, "Rename", "A name cannot be empty or contain any of  \\ / : * ? \" < > |");
            return;
        }

        // A file keeps its extension (it is what decides its type), unless it was typed in full
        std::string extension = fs::is_directory(oldPath) ? "" : oldPath.extension().string();
        auto lower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; };
        if (!extension.empty() && lower(name).size() >= extension.size() && lower(name).compare(lower(name).size() - extension.size(), extension.size(), lower(extension)) == 0)
            extension.clear();

        fs::path newPath = oldPath.parent_path() / (name + extension);

        if (newPath == oldPath)
            return;

        AssetOperations::Result r = AssetOperations::Move(renamePath, Engine::Filesystem::Path(newPath.string()));
        if (!r.ok)
            Popups::Show(Popups::PopupType::Error, "Rename", r.message);

        selection.Clear();
        dirty = true;
    }

    void AssetBrowser::DrawDialogs()
    {
        if (openRenamePopup)
        {
            ImGui::OpenPopup("Rename Asset");
            openRenamePopup = false;
        }

        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextDisabled("%s", Engine::Filesystem::Path(renamePath.full).GetFilename().c_str());

            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            ImGui::SetNextItemWidth(280.0f);
            bool confirmed = ImGui::InputText("##rename", renameBuffer, sizeof(renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

            ImGui::Spacing();

            if (ImGui::Button("Rename", ImVec2(100.0f, 0.0f)))
                confirmed = true;
            ImGui::SameLine();
            const bool cancelled = ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape);

            if (confirmed)
            {
                ApplyRename();
                ImGui::CloseCurrentPopup();
            }
            else if (cancelled)
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    void AssetBrowser::NavigateTo(const std::string& path)
    {
        Engine::Filesystem::Path target(path);

        if (!target.Exists() || !target.IsDirectory())
            return;

        currentPath = target;
        dirty = true; // mark for refresh
    }
}