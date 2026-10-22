#include "asset_browser.hpp"

#include "apps/editor/gui/main_window.hpp"

#include "engine/assets/project/project.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/world/world.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/assets/hot_reloader.hpp"
#include "engine/assets/asset_importer.hpp"
#include "engine/platform/clipboard/clipboard.hpp"
#include "engine/platform/windowing/iwindow.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/features/immediate/thumbnail_service.hpp"

#include "apps/editor/gui/panels/asset_editor_registry.hpp"
#include "apps/editor/gui/dragdrop/asset_drag_drop.hpp"
#include "apps/editor/gui/IconsLucide.h"
#include "apps/editor/gui/popups.hpp"
#include "apps/editor/gui/notifications.hpp"
#include "apps/editor/gui/panels/asset_browser/asset_operations.hpp"
#include "apps/editor/gui/panels/asset_browser/sound_thumbnail.hpp"

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
            case Engine::Filesystem::Type::T_PROBES:         return IM_COL32(236, 96, 160, 255);  // pink
            case Engine::Filesystem::Type::T_LIGHTMAP:       return IM_COL32(244, 143, 177, 255); // light pink
            case Engine::Filesystem::Type::T_IMAGE:          return IM_COL32(175, 110, 220, 255); // purple/pink
            case Engine::Filesystem::Type::T_SOUND:          return IM_COL32(26, 188, 156, 255);  // teal
            case Engine::Filesystem::Type::T_SCRIPT:         return IM_COL32(241, 196, 15, 255);  // yellow
            case Engine::Filesystem::Type::T_WORLD:          return IM_COL32(231, 76, 60, 255);   // red
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
            case Engine::Filesystem::Type::T_PROBES:         return "Probe Bake";
            case Engine::Filesystem::Type::T_LIGHTMAP:       return "Lightmap Bake";
            case Engine::Filesystem::Type::T_IMAGE:          return "Texture";
            case Engine::Filesystem::Type::T_SOUND:          return "Sound";
            case Engine::Filesystem::Type::T_SCRIPT:         return "Script";
            case Engine::Filesystem::Type::T_WORLD:          return "World";
            case Engine::Filesystem::Type::T_SHADER:         return "Shader";
            case Engine::Filesystem::Type::T_COMPUTE_SHADER: return "Compute Shader";
            case Engine::Filesystem::Type::T_FONT:           return "Font";
            case Engine::Filesystem::Type::T_CONFIG:         return "Config";
            case Engine::Filesystem::Type::T_TEXT:           return "Text File";
            default:                                         return "Asset";
        }
    }

    // Fills the request for the thumbnail the ThumbnailService makes of this asset : meshes and materials are
    // rendered, sounds get their waveform drawn. False for the rest (images sample their own texture
    // directly, every other type shows its icon).
    static bool MakeThumbnailRequest(Engine::Filesystem::Type type, const std::string& nameInProject, const Engine::Filesystem::Path& path,
                                     Engine::Rendering::ThumbnailRequest& request)
    {
        switch (type)
        {
            case Engine::Filesystem::Type::T_MODEL:
                request.kind = Engine::Rendering::ThumbnailKind::Mesh;
                break;
            case Engine::Filesystem::Type::T_MATERIAL:
                request.kind = Engine::Rendering::ThumbnailKind::Material;
                break;
            case Engine::Filesystem::Type::T_SOUND:
                request.kind = Engine::Rendering::ThumbnailKind::Sound;
                request.generate = &DrawSoundThumbnail;
                request.generatorVersion = kSoundThumbnailVersion;
                break;
            default:
                return false;
        }

        request.nameInProject = nameInProject;
        request.filePath = path;
        return true;
    }

    static bool MakeThumbnailRequest(const Asset& item, Engine::Rendering::ThumbnailRequest& request)
    {
        return !item.isDirectory && MakeThumbnailRequest(item.type, item.nameInProject, item.path, request);
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

        const bool searching = searchBuffer[0] != '\0';

        auto files = fileManager->ListDirectory(
            currentPath,
            allowedTypes,
            true,
            searching && searchSubfolders
        );

        // Only a change of folder / search leaves the folder structure as it was
        if (!navigationOnly)
            treeDirty = true;
        navigationOnly = false;

        auto lower = [](std::string v) { std::transform(v.begin(), v.end(), v.begin(), ::tolower); return v; };
        const std::string needle = lower(searchBuffer);

        auto atlas = EditorResources::Instance().GetIconAtlas();

        visibleItemCount = static_cast<int>(files.size());
        items.reserve(files.size());

        for (const auto& file : files)
        {
            if (file.isDirectory ? !showFolders : typeHidden[(int)file.type])
                continue;

            Asset asset(
                ImHashStr(file.path.full.c_str()),
                file.path,
                file.type,
                file.isDirectory,
                &atlas->GetRegion(file.isDirectory ? Engine::Filesystem::Type::T_DIRECTORY : file.type),
                file.nameInProject,
                file.size
            );

            // A search matches the name or the kind of asset ("material", "texture"...)
            if (searching
                && asset.lowerName.find(needle) == std::string::npos
                && lower(GetTypeDisplayName(file.type, file.isDirectory)).find(needle) == std::string::npos)
                continue;

            items.push_back(std::move(asset));
        }

        // Folders first, then the chosen order, the name settling ties
        const SortKey key = sortKey;
        const bool ascending = sortAscending;
        std::stable_sort(items.begin(), items.end(), [key, ascending](const Asset& a, const Asset& b)
        {
            if (a.isDirectory != b.isDirectory)
                return a.isDirectory;

            int delta = 0;
            if (key == SortKey::Type && !a.isDirectory)
                delta = (int)a.type - (int)b.type;
            else if (key == SortKey::Size && !a.isDirectory)
                delta = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);

            if (delta != 0)
                return ascending ? delta < 0 : delta > 0;

            int byName = a.lowerName.compare(b.lowerName);
            return ascending ? byName < 0 : byName > 0;
        });

        dirty = false;

        // RevealAsset : select the asset once its folder is listed
        if (!pendingRevealPath.empty())
        {
            selection.Clear();
            for (int i = 0; i < static_cast<int>(items.size()); i++)
            {
                if (items[i].path.full == pendingRevealPath)
                {
                    selection.SetItemSelected(items[i].id, true);
                    pendingScrollItem = i;
                    break;
                }
            }
            pendingRevealPath.clear();
        }
    }

    void AssetBrowser::Draw()
    {
        // Files added, renamed, deleted or changed from outside the editor
        if (auto* hotReloader = Engine::Core::GetEngine().GetHotReloader())
        {
            if (!hotReloadListening)
            {
                hotReloadListening = true;
                hotReloader->AddChangeListener([stale = staleThumbnails](const std::string& name) { stale->push_back(name); });
            }

            if (hotReloader->GetRevision() != hotReloadRevision)
            {
                hotReloadRevision = hotReloader->GetRevision();
                dirty = true; navigationOnly = false;
            }
        }

        if (!staleThumbnails->empty())
        {
            auto* thumbnailService = Engine::Core::GetEngine().GetRenderer()->GetThumbnailService();
            auto* assetIDs = Engine::Core::GetEngine().GetAssetIDManager();
            for (const std::string& name : *staleThumbnails)
            {
                auto info = assetIDs->GetAssetFromID(assetIDs->GetIDFromNameInProject(name));
                Engine::Rendering::ThumbnailRequest request;
                if (info && info->baseInfos.nameInProject == name && MakeThumbnailRequest(info->baseInfos.type, name, info->baseInfos.path, request))
                    thumbnailService->Invalidate(request);
            }
            staleThumbnails->clear();
        }

        ProcessImports();

        if (dirty)
            Refresh();

        Engine::Core::GetEngine().GetRenderer()->GetThumbnailService()->BeginFrame();

        ImGui::Begin("Asset Browser");

        // Mouse back / forward buttons and Backspace, like a file explorer
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        {
            if (ImGui::IsMouseClicked(3))
                NavigateHistory(-1);
            else if (ImGui::IsMouseClicked(4))
                NavigateHistory(+1);
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
        {
            const ImGuiIO& io = ImGui::GetIO();

            if (io.KeyAlt)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
                    NavigateHistory(-1);
                else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
                    NavigateHistory(+1);
                else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                    GoUp();
            }

            if (io.KeyCtrl && !io.KeyAlt)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_F, false))
                    focusSearch = true;
                if (!io.WantTextInput)
                {
                    if (ImGui::IsKeyPressed(ImGuiKey_B, false))
                        showSidebar = !showSidebar;
                    if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
                        SetZoom(thumbnailSize * 1.15f);
                    if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
                        SetZoom(thumbnailSize / 1.15f);
                    if (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false))
                        SetZoom(80.0f);
                    if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N, false))
                        pendingAction = Action::NewFolder;
                    if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_I, false))
                        pendingAction = Action::Import;
                }
            }

            if (!io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Backspace))
                GoUp();
        }

        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        DrawBreadcrumb();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        if (showSidebar)
            DrawSidebar(ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing());

        DrawAssets();

        ProcessAction();
        DrawDialogs();

        ImGui::End();
    }

    void AssetBrowser::DrawBreadcrumb()
    {
        auto& engine = Engine::Core::GetEngine();
        const ImGuiStyle& style = ImGui::GetStyle();

        Engine::Filesystem::Path projectRoot = engine.GetCurrentProject()->GetProjectResourcesPath();

        // currentPath relative to projectRoot (NOT the other way around - that would compute the
        // path FROM currentPath back up TO the root, i.e. "..", which is backwards for a breadcrumb).
        std::string relative = currentPath.RelativeTo(projectRoot).full;
        if (relative == ".")
            relative.clear();

        // Back / forward / up
        {
            const bool canBack = historyIndex > 0;
            const bool canForward = historyIndex + 1 < (int)history.size();
            const bool canUp = !relative.empty();

            ImGui::BeginDisabled(!canBack);
            if (ImGui::Button(ICON_LC_ARROW_LEFT "##Back"))
                NavigateHistory(-1);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Back (Alt+Left)");

            ImGui::SameLine();
            ImGui::BeginDisabled(!canForward);
            if (ImGui::Button(ICON_LC_ARROW_RIGHT "##Forward"))
                NavigateHistory(+1);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Forward (Alt+Right)");

            ImGui::SameLine();
            ImGui::BeginDisabled(!canUp);
            if (ImGui::Button(ICON_LC_ARROW_UP "##Up"))
                GoUp();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Parent folder (Alt+Up)");

            ImGui::SameLine();
        }

        const float breadcrumbStartX = ImGui::GetCursorPosX();
        const float rowY = ImGui::GetCursorPosY();

        // Right side : sidebar toggle, filters, view options, search
        const float frameHeight = ImGui::GetFrameHeight();
        const float spacing = style.ItemSpacing.x;
        const float filterWidth = 96.0f;
        const float searchWidth = 170.0f;
        const float clusterWidth = frameHeight + spacing + frameHeight + spacing + filterWidth + spacing + frameHeight + spacing + searchWidth;
        const float clusterX = breadcrumbStartX + ImGui::GetContentRegionAvail().x - clusterWidth;

        ImGui::SetCursorPos(ImVec2(clusterX, rowY));

        if (ImGui::Button(ICON_LC_IMPORT "##Import", ImVec2(frameHeight, 0.0f)))
            pendingAction = Action::Import;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Import assets from the disk (Ctrl+I)");

        ImGui::SameLine();
        {
            const bool sidebarOn = showSidebar;
            if (sidebarOn)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(ICON_LC_PANEL_LEFT "##Sidebar", ImVec2(frameHeight, 0.0f)))
                showSidebar = !showSidebar;
            if (sidebarOn)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Folder tree (Ctrl+B)");
        }

        ImGui::SameLine();
        {
            const int active = ActiveFilterCount();
            if (active > 0)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            std::string filterLabel = std::string(ICON_LC_LIST_FILTER) + (active > 0 ? " Filters (" + std::to_string(active) + ")" : " Filters") + "##Filters";
            if (ImGui::Button(filterLabel.c_str(), ImVec2(filterWidth, 0.0f)))
                ImGui::OpenPopup("##FilterPopup");
            if (active > 0)
                ImGui::PopStyleColor();
            DrawFilterPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(ICON_LC_SLIDERS_HORIZONTAL "##View", ImVec2(frameHeight, 0.0f)))
            ImGui::OpenPopup("##ViewPopup");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("View options");
        DrawViewPopup();

        ImGui::SameLine();
        if (focusSearch)
        {
            ImGui::SetKeyboardFocusHere();
            focusSearch = false;
        }
        ImGui::SetNextItemWidth(searchWidth);
        if (ImGui::InputTextWithHint("##AssetSearch", ICON_LC_SEARCH "  Search (Ctrl+F)", searchBuffer, sizeof(searchBuffer)))
            RequestRelist();
        if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape) && searchBuffer[0] != '\0')
        {
            searchBuffer[0] = '\0';
            RequestRelist();
        }

        // Left side : the breadcrumb, its leftmost folders folded in a "..." when it does not fit
        ImGui::SetCursorPos(ImVec2(breadcrumbStartX, rowY));

        struct Crumb { std::string label; Engine::Filesystem::Path path; };
        std::vector<Crumb> crumbs;
        crumbs.push_back({ projectRoot.GetFilename(), projectRoot });
        {
            std::stringstream ss(relative);
            std::string segment;
            Engine::Filesystem::Path accum = projectRoot;
            while (std::getline(ss, segment, '/'))
            {
                if (segment.empty())
                    continue;
                accum = accum / segment;
                crumbs.push_back({ segment, accum });
            }
        }

        const float separatorWidth = ImGui::CalcTextSize(">").x;
        const float available = clusterX - breadcrumbStartX - spacing;
        auto crumbWidth = [&](const std::string& label) { return ImGui::CalcTextSize(label.c_str()).x + 12.0f; };

        size_t first = 0;
        auto totalWidth = [&]()
        {
            float total = 0.0f;
            if (first > 0)
                total += crumbWidth("...") + separatorWidth;
            for (size_t i = first; i < crumbs.size(); i++)
                total += crumbWidth(crumbs[i].label) + (i > first ? separatorWidth : 0.0f);
            return total;
        };
        while (first + 1 < crumbs.size() && totalWidth() > available)
            first++;

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

        auto drawCrumb = [&](int id, const std::string& label, bool isCurrent, const Engine::Filesystem::Path& target)
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
                    NavigateTo(target.full);
                ImGui::PopStyleColor();

                // Dropping assets on a parent folder moves them there
                if (ImGui::BeginDragDropTarget())
                {
                    std::vector<std::string> dropped = DragDrop::AcceptAssetDragDropPayload();
                    if (!dropped.empty())
                        MoveItemsInto(dropped, target);
                    ImGui::EndDragDropTarget();
                }
            }

            ImGui::PopID();
        };

        if (first > 0)
        {
            drawCrumb(-1, "...", false, crumbs[first - 1].path);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", crumbs[first - 1].path.full.c_str());
        }

        for (size_t i = first; i < crumbs.size(); i++)
        {
            if (i > first || first > 0)
                drawSeparator();
            drawCrumb((int)i, crumbs[i].label, i == crumbs.size() - 1, crumbs[i].path);
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);
    }

    int AssetBrowser::ActiveFilterCount() const
    {
        int count = showFolders ? 0 : 1;
        for (int i = 0; i < 13; i++)
            if (typeHidden[i])
                count++;
        return count;
    }

    void AssetBrowser::DrawFilterPopup()
    {
        if (!ImGui::BeginPopup("##FilterPopup"))
            return;

        bool changed = false;

        ImGui::TextDisabled("Show");
        changed |= ImGui::Checkbox("Folders", &showFolders);
        ImGui::Separator();

        using Engine::Filesystem::Type;
        static const Type kTypes[] = {
            Type::T_MODEL, Type::T_MATERIAL, Type::T_IMAGE, Type::T_SOUND, Type::T_WORLD, Type::T_SCRIPT, Type::T_SHADER,
            Type::T_COMPUTE_SHADER, Type::T_FONT, Type::T_PROBES, Type::T_LIGHTMAP, Type::T_TEXT, Type::T_CONFIG
        };

        for (Type type : kTypes)
        {
            bool shown = !typeHidden[(int)type];

            ImGui::PushID((int)type);
            ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGui::ColorConvertU32ToFloat4(GetTypeAccentColor(type, false)));
            if (ImGui::Checkbox(GetTypeDisplayName(type, false), &shown))
            {
                typeHidden[(int)type] = !shown;
                changed = true;
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }

        ImGui::Separator();
        if (ImGui::Button("All", ImVec2(60.0f, 0.0f)))
        {
            std::fill(std::begin(typeHidden), std::end(typeHidden), false);
            showFolders = true;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("None", ImVec2(60.0f, 0.0f)))
        {
            for (Type type : kTypes)
                typeHidden[(int)type] = true;
            changed = true;
        }

        ImGui::Separator();
        changed |= ImGui::Checkbox("Search in sub folders", &searchSubfolders);

        if (changed)
            RequestRelist();

        ImGui::EndPopup();
    }

    void AssetBrowser::DrawViewPopup()
    {
        if (!ImGui::BeginPopup("##ViewPopup"))
            return;

        ImGui::TextDisabled("View");
        if (ImGui::RadioButton(ICON_LC_LAYOUT_GRID "  Tiles", !listView))
            listView = false;
        if (ImGui::RadioButton(ICON_LC_LIST "  List", listView))
            listView = true;

        ImGui::Separator();
        ImGui::TextDisabled("Sort by");

        bool resort = false;
        resort |= ImGui::RadioButton("Name", sortKey == SortKey::Name) && (sortKey = SortKey::Name, true);
        resort |= ImGui::RadioButton("Type", sortKey == SortKey::Type) && (sortKey = SortKey::Type, true);
        resort |= ImGui::RadioButton("Size", sortKey == SortKey::Size) && (sortKey = SortKey::Size, true);
        resort |= ImGui::Checkbox("Ascending", &sortAscending);
        if (resort)
            RequestRelist();

        ImGui::Separator();
        float zoom = thumbnailSize;
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::SliderFloat("##Zoom", &zoom, 40.0f, 160.0f, "Size %.0f"))
            SetZoom(zoom);

        ImGui::Checkbox("Folder tree", &showSidebar);

        ImGui::EndPopup();
    }

    void AssetBrowser::SetZoom(float size)
    {
        thumbnailSize = IM_CLAMP(size, 40.0f, 160.0f);
    }

    void AssetBrowser::RebuildFolderTree()
    {
        namespace fs = std::filesystem;
        treeDirty = false;

        const Engine::Filesystem::Path root = Engine::Core::GetEngine().GetCurrentProject()->GetProjectResourcesPath();

        folderTree = FolderNode{};
        folderTree.name = root.GetFilename();
        folderTree.path = root.full;

        auto lower = [](std::string v) { std::transform(v.begin(), v.end(), v.begin(), ::tolower); return v; };

        std::function<void(FolderNode&, int)> fill = [&](FolderNode& node, int depth)
        {
            if (depth > 16)
                return;

            std::error_code ec;
            for (fs::directory_iterator it(node.path, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
            {
                if (!it->is_directory(ec))
                    continue;

                const std::string name = it->path().filename().string();
                if (name.empty() || name[0] == '.')
                    continue;

                FolderNode child;
                child.name = name;
                child.path = it->path().string();
                node.children.push_back(std::move(child));
            }

            std::sort(node.children.begin(), node.children.end(), [&](const FolderNode& a, const FolderNode& b) { return lower(a.name) < lower(b.name); });

            for (FolderNode& child : node.children)
                fill(child, depth + 1);
        };

        fill(folderTree, 0);
    }

    void AssetBrowser::DrawFolderNode(const FolderNode& node, bool isRoot)
    {
        const bool isCurrent = node.path == currentPath.full;
        const bool isAncestor = currentPath.full.size() > node.path.size() && currentPath.full.compare(0, node.path.size(), node.path) == 0 && currentPath.full[node.path.size()] == '/';

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
        if (node.children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (isCurrent)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (isRoot)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;

        if (treeRevealCurrent && isAncestor)
            ImGui::SetNextItemOpen(true);

        const bool wasOpen = ImGui::TreeNodeGetOpen(ImGui::GetID(("###" + node.path).c_str()));
        const std::string label = std::string(wasOpen && !node.children.empty() ? ICON_LC_FOLDER_OPEN : ICON_LC_FOLDER) + "  " + node.name + "###" + node.path;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
        ImGui::PopStyleVar();

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
            NavigateTo(node.path);

        if (ImGui::BeginDragDropTarget())
        {
            std::vector<std::string> dropped = DragDrop::AcceptAssetDragDropPayload();
            if (!dropped.empty())
                MoveItemsInto(dropped, Engine::Filesystem::Path(node.path));
            ImGui::EndDragDropTarget();
        }

        if (open)
        {
            for (const FolderNode& child : node.children)
                DrawFolderNode(child, false);
            ImGui::TreePop();
        }
    }

    void AssetBrowser::DrawSidebar(float height)
    {
        if (treeDirty)
            RebuildFolderTree();

        ImGui::BeginChild("##FolderTree", ImVec2(sidebarWidth, height), ImGuiChildFlags_Borders);
        DrawFolderNode(folderTree, true);
        treeRevealCurrent = false;
        ImGui::EndChild();

        // Splitter
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton("##SidebarSplitter", ImVec2(6.0f, height));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive())
            sidebarWidth = IM_CLAMP(sidebarWidth + ImGui::GetIO().MouseDelta.x, 120.0f, 500.0f);
        ImGui::SameLine(0.0f, 0.0f);
    }

    void AssetBrowser::DrawAssets()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowContentSize(ImVec2(0.0f, LayoutOuterPadding + LayoutLineCount * LayoutItemStep.y));
        if (ImGui::BeginChild("Assets", ImVec2(0.0f, -ImGui::GetTextLineHeightWithSpacing()), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoMove))
        {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            const float avail_width = ImGui::GetContentRegionAvail().x;
            UpdateLayoutSizes(avail_width);

            if (pendingScrollItem >= 0)
            {
                const float line = (float)(pendingScrollItem / LayoutColumnCount);
                ImGui::SetScrollY(IM_MAX(line * LayoutItemStep.y - LayoutItemStep.y, 0.0f));
                pendingScrollItem = -1;
            }

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
            else if ((ImGui::Shortcut(ImGuiKey_Enter) || ImGui::Shortcut(ImGuiKey_KeypadEnter)) && selection.Size >= 1)
                pendingAction = Action::Open;

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
                    const int item_max_idx_for_current_line = IM_MIN((line_idx + 1) * column_count, static_cast<int>(items.size()));
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
                                    auto tex = Engine::Core::GetEngine().GetResourcesManager()->Get<Engine::Rendering::Texture2D>(Engine::Core::Resources::AssetKind::Texture, item_data->nameInProject);
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
                        // (viewport, world tree, inspector asset fields, material slots, ...) can feed
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

                        // Dropping assets on a folder moves them into it
                        if (item_data->isDirectory && ImGui::BeginDragDropTarget())
                        {
                            std::vector<std::string> dropped = DragDrop::AcceptAssetDragDropPayload();
                            if (!dropped.empty())
                                MoveItemsInto(dropped, item_data->path);
                            ImGui::EndDragDropTarget();
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

                            ImVec2 icon_min, icon_max;

                            if (listView)
                            {
                                const float rowHeight = LayoutItemSize.y;

                                if (item_idx & 1)
                                    draw_list->AddRectFilled(pos, ImVec2(pos.x + LayoutItemSize.x, pos.y + rowHeight), IM_COL32(255, 255, 255, 6));
                                draw_list->AddRectFilled(ImVec2(pos.x, pos.y + 3), ImVec2(pos.x + 3, pos.y + rowHeight - 3), accent, 1.5f);

                                icon_min = ImVec2(pos.x + 10.0f, pos.y + (rowHeight - ThumbnailSize.y) * 0.5f);
                                icon_max = ImVec2(icon_min.x + ThumbnailSize.x, icon_min.y + ThumbnailSize.y);
                            }
                            else
                            {
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

                                icon_min = ImVec2(
                                    box_min.x + icon_offset.x,
                                    box_min.y + icon_offset.y
                                );

                                icon_max = ImVec2(
                                    icon_min.x + ThumbnailSize.x,
                                    icon_min.y + ThumbnailSize.y
                                );
                            }

                            // Real preview for images, meshes, materials and sounds; every other type
                            // falls back to its atlas icon. Images sample their own already-loaded
                            // texture directly; meshes and materials are rendered lit into the
                            // thumbnail atlas the first time they're seen, and sounds get their
                            // waveform drawn there (see ThumbnailService) - the icon shows until then.
                            std::shared_ptr<Engine::Rendering::Texture2D> imageThumbnail;
                            Engine::Rendering::ThumbnailRegion assetThumbnail;
                            bool hasAssetThumbnail = false;
                            auto* thumbnails = Engine::Core::GetEngine().GetRenderer()->GetThumbnailService();

                            Engine::Rendering::ThumbnailRequest request;
                            if (!item_data->isDirectory && item_data->type == Engine::Filesystem::Type::T_IMAGE)
                                imageThumbnail = Engine::Core::GetEngine().GetResourcesManager()->Get<Engine::Rendering::Texture2D>(Engine::Core::Resources::AssetKind::Texture, item_data->nameInProject);
                            else if (MakeThumbnailRequest(*item_data, request))
                                hasAssetThumbnail = thumbnails->Get(request, assetThumbnail);

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
                            else if (hasAssetThumbnail)
                            {
                                draw_list->AddImage(
                                    (void*)(intptr_t)thumbnails->GetAtlasTexture(),
                                    icon_min,
                                    icon_max,
                                    ImVec2(assetThumbnail.uv0.x, assetThumbnail.uv0.y),
                                    ImVec2(assetThumbnail.uv1.x, assetThumbnail.uv1.y)
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

                            if (listView)
                            {
                                const float rowHeight = LayoutItemSize.y;
                                const float fontSize = ImGui::GetFontSize();
                                const float textY = pos.y + (rowHeight - fontSize) * 0.5f;
                                const float nameX = icon_max.x + 10.0f;
                                const float right = pos.x + LayoutItemSize.x - 10.0f;
                                const float sizeColumn = 80.0f;
                                const float typeColumn = 140.0f;
                                const float typeX = right - sizeColumn - typeColumn;
                                const bool showColumns = typeX > nameX + 140.0f;

                                ImU32 label_col = ImGui::GetColorU32(item_is_selected ? ImGuiCol_Text : ImGuiCol_TextDisabled);

                                draw_list->PushClipRect(ImVec2(nameX, pos.y), ImVec2(showColumns ? typeX - 8.0f : right, pos.y + rowHeight), true);
                                draw_list->AddText(ImVec2(nameX, textY), label_col, item_data->filename.c_str());
                                draw_list->PopClipRect();

                                if (showColumns)
                                {
                                    draw_list->AddText(ImVec2(typeX, textY), accent, GetTypeDisplayName(item_data->type, item_data->isDirectory));

                                    if (!item_data->isDirectory)
                                    {
                                        std::string sizeText = FormatFileSize(item_data->size);
                                        draw_list->AddText(ImVec2(right - ImGui::CalcTextSize(sizeText.c_str()).x, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), sizeText.c_str());
                                    }
                                }
                            }
                            else
                            {
                                // Type-color accent bar, sitting right under the thumbnail - the main
                                // "what kind of asset is this" cue, at a glance, across the whole grid.
                                ImVec2 bar_min(box_min.x + 3, icon_max.y + 4);
                                ImVec2 bar_max(box_max.x - 3, bar_min.y + 3);
                                draw_list->AddRectFilled(bar_min, bar_max, accent, 1.5f);

                                if (display_label)
                                {
                                    const float labelMaxWidth = (box_max.x - box_min.x) - 8.0f;
                                    if (item_data->wrapWidth != labelMaxWidth)
                                    {
                                        WrapLabelToLines(item_data->filename, labelMaxWidth, item_data->line1, item_data->line2);
                                        item_data->wrapWidth = labelMaxWidth;
                                    }
                                    const std::string& line1 = item_data->line1;
                                    const std::string& line2 = item_data->line2;

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
            if (!listView && ImGui::IsWindowHovered() && io.MouseWheel != 0.0f && ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsAnyItemActive() == false)
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
                    SetZoom(thumbnailSize * powf(1.1f, (float)(int)zoomWheelAccum));
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

        // Status line
        if (searchBuffer[0] != '\0')
            ImGui::TextDisabled("%d of %d items", (int)items.size(), visibleItemCount);
        else
            ImGui::TextDisabled("%d items", (int)items.size());
        if (selection.Size > 0)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%d selected)", (int)selection.Size);
        }

        // Zoom and view mode, pinned to the right
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            const float frameHeight = ImGui::GetFrameHeight();
            const float sliderWidth = listView ? 0.0f : 110.0f;
            const float totalWidth = frameHeight * 2.0f + style.ItemSpacing.x + (listView ? 0.0f : sliderWidth + style.ItemSpacing.x);

            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - totalWidth);

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 0.0f));
            if (!listView)
            {
                float zoom = thumbnailSize;
                ImGui::SetNextItemWidth(sliderWidth);
                if (ImGui::SliderFloat("##StatusZoom", &zoom, 40.0f, 160.0f, ""))
                    SetZoom(zoom);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Thumbnail size (Ctrl+Wheel)");
                ImGui::SameLine();
            }

            if (ImGui::Selectable(ICON_LC_LAYOUT_GRID "##Tiles", !listView, 0, ImVec2(frameHeight, 0.0f)))
                listView = false;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tiles");
            ImGui::SameLine();
            if (ImGui::Selectable(ICON_LC_LIST "##List", listView, 0, ImVec2(frameHeight, 0.0f)))
                listView = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("List");
            ImGui::PopStyleVar();
        }
    }

    void AssetBrowser::UpdateLayoutSizes(float avail_width)
    {
        if (listView)
        {
            ThumbnailSize = ImVec2(20.0f, 20.0f);
            Spacing = ImVec2(8.0f, 2.0f);
            ItemSize = ImVec2(IM_MAX(avail_width - Spacing.x, 100.0f), 26.0f);
        }
        else
        {
            ThumbnailSize = ImVec2(thumbnailSize, thumbnailSize);
            const float gap = IM_CLAMP(thumbnailSize * 0.2f, 8.0f, 20.0f);
            Spacing = ImVec2(gap, gap);
            ItemSize = ImVec2(thumbnailSize + 10.0f, thumbnailSize + 58.0f);
        }

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

    void AssetBrowser::RequestOpenWorld(const Engine::Filesystem::Path &path)
    {
        auto& engine = Engine::Core::GetEngine();
        auto* worldManager = engine.GetWorldManager();

        if (worldManager->IsAsyncLoadInProgress())
            return;

        // Whatever's currently loaded is only torn down once the new world has resolved
        // successfully (see WorldManager::FinishAsyncLoad) - so a bad/missing target here can't
        // leave the editor with zero worlds loaded.
        std::string pathInProject = engine.GetFileManager()->GetFileInfos(path).nameInProject;

        auto* current = worldManager->GetWorldAt(0);
        if (current && current->IsDirty())
        {
            GUI::Popups::ConfirmUnsavedChanges(current->GetName(),
                [worldManager, current, pathInProject]()
                {
                    current->Serialize(current->GetPath());
                    worldManager->LoadWorldAsync(pathInProject);
                },
                [worldManager, pathInProject]()
                {
                    worldManager->LoadWorldAsync(pathInProject);
                });
            return;
        }

        worldManager->LoadWorldAsync(pathInProject);
    }

    void AssetBrowser::SetParentWindow(Core::EditorMainWindow* parent)
    {
        this->parent = parent;
    }

    void AssetBrowser::OpenItem(const Asset& item)
    {
        if (item.isDirectory)
            NavigateTo(item.path.full);
        else if (item.type == Engine::Filesystem::Type::T_WORLD)
            RequestOpenWorld(item.path);
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
            if (ImGui::MenuItem("Open", "Enter"))
                pendingAction = Action::Open;
            if (single && std::filesystem::path(target->path.full).parent_path() != std::filesystem::path(currentPath.full)
                && ImGui::MenuItem("Show in Folder"))
                pendingAction = Action::ShowInFolder;
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

            bool hasThumbnail = false;
            Engine::Rendering::ThumbnailRequest probe;
            for (const Asset* item : GetSelectedItems())
                hasThumbnail |= MakeThumbnailRequest(*item, probe);
            if (hasThumbnail && ImGui::MenuItem("Refresh Thumbnail"))
                pendingAction = Action::RefreshThumbnail;
        }
        else
        {
            if (ImGui::BeginMenu("Create"))
            {
                if (ImGui::MenuItem("Folder", "Ctrl+Shift+N"))
                    pendingAction = Action::NewFolder;
                if (ImGui::MenuItem("Material"))
                    pendingAction = Action::NewMaterial;
                if (ImGui::MenuItem("World"))
                    pendingAction = Action::NewWorld;
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste))
            {
                pendingAction = Action::Paste;
                pasteTargetDir = Engine::Filesystem::Path("");
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Import...", "Ctrl+I"))
                pendingAction = Action::Import;

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
                // A folder is entered, the rest is opened in its editor (a few at most, a whole selection is not meant to be opened)
                if (selected.size() == 1 || (selected.size() <= 5 && std::none_of(selected.begin(), selected.end(), [](const Asset* a) { return a->isDirectory || a->type == Engine::Filesystem::Type::T_WORLD; })))
                {
                    for (const Asset* item : selected)
                        OpenItem(*item);
                }
                break;

            case Action::ShowInFolder:
                if (selected.size() == 1)
                {
                    const std::string full = selected[0]->path.full;
                    searchBuffer[0] = '\0';
                    NavigateTo(std::filesystem::path(full).parent_path().string());
                    pendingRevealPath = full;
                }
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

            case Action::RefreshThumbnail:
                for (const Asset* item : selected)
                {
                    Engine::Rendering::ThumbnailRequest request;
                    if (MakeThumbnailRequest(*item, request))
                        Engine::Core::GetEngine().GetRenderer()->GetThumbnailService()->Invalidate(request);
                }
                break;

            case Action::NewFolder:
            case Action::NewMaterial:
            case Action::NewWorld:
            {
                Path created;
                Ops::Result r = action == Action::NewFolder ? Ops::CreateFolder(currentPath, created)
                              : action == Action::NewMaterial ? Ops::CreateMaterial(currentPath, created)
                              : Ops::CreateWorld(currentPath, created);
                if (!r.ok)
                    failures.push_back(r.message);
                else
                    BeginRename(created); // name it right away, like every content browser does
                changed = true;
                break;
            }

            case Action::Import:
                OpenImportDialog();
                break;

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
            dirty = true; navigationOnly = false;
        }

        ReportFailures("Asset Browser", failures);
    }

    void AssetBrowser::OpenImportDialog()
    {
        namespace Dialogs = Engine::Core::Platform::Dialogs;

        // One dialog at a time
        if (importQueue->dialogOpen.exchange(true))
            return;

        // What the engine reads, then what the importers of the plugins convert
        const std::vector<Dialogs::FileFilter> groups = {
            { "Models", "fbx" },
            { "Images", "png;jpg;jpeg;hdr;bmp;gif;tga" },
            { "Sounds", "wav;mp3;ogg" },
            { "Fonts", "ttf;otf;fnt" },
            { "Shaders", "vert;frag;geom;comp" }
        };

        std::string converted;
        for (const std::string& extension : Engine::Filesystem::AssetImporters::Get().Extensions())
            converted += (converted.empty() ? "" : ";") + (!extension.empty() && extension[0] == '.' ? extension.substr(1) : extension);

        Dialogs::FileDialogDesc desc;
        desc.title = "Import assets";
        desc.allowMultiple = true;
        desc.parentWindow = Engine::Core::GetEngine().GetWindow()->GetNativeHandle();

        std::string everything;
        for (const Dialogs::FileFilter& group : groups)
            everything += (everything.empty() ? "" : ";") + group.pattern;
        if (!converted.empty())
            everything += ";" + converted;

        desc.filters.push_back({ "All supported assets", everything });
        desc.filters.insert(desc.filters.end(), groups.begin(), groups.end());
        if (!converted.empty())
            desc.filters.push_back({ "Converted formats", converted });
        desc.filters.push_back({ "All files", "*" });

        {
            std::lock_guard<std::mutex> lock(importQueue->mutex);
            desc.defaultPath = importQueue->lastFolder;
        }

        // The dialog is asynchronous and answers from its own thread : the answer is queued for ProcessImports. It goes to the folder that is
        // open now, whatever the user browses to meanwhile.
        Dialogs::ShowOpenFileDialog(desc, [queue = importQueue, destination = currentPath](const std::vector<std::string>& paths)
        {
            std::lock_guard<std::mutex> lock(queue->mutex);
            if (!paths.empty())
            {
                queue->lastFolder = std::filesystem::u8path(paths[0]).parent_path().u8string();
                queue->requests.push_back({ destination, paths });
            }
            queue->dialogOpen = false;
        });
    }

    void AssetBrowser::ProcessImports()
    {
        std::vector<ImportRequest> requests;
        {
            std::lock_guard<std::mutex> lock(importQueue->mutex);
            requests.swap(importQueue->requests);
        }

        for (const ImportRequest& request : requests)
        {
            AssetOperations::ImportSummary summary = AssetOperations::Import(request.destination, request.sources);

            if (summary.imported > 0)
            {
                Notifications::Success("Imported %d asset%s", summary.imported, summary.imported > 1 ? "s" : "");
                dirty = true; navigationOnly = false;
            }
            ReportFailures("Import", summary.failures);
        }
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
                    dirty = true; navigationOnly = false;
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
        dirty = true; navigationOnly = false;
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

        if (!navigatingHistory && !(historyIndex >= 0 && history[historyIndex] == target.full))
        {
            // A new destination drops whatever "forward" had
            history.resize(historyIndex + 1);
            history.push_back(target.full);
            historyIndex = (int)history.size() - 1;
        }

        currentPath = target;
        selection.Clear();
        treeRevealCurrent = true;
        RequestRelist(); // mark for refresh
    }

    void AssetBrowser::NavigateHistory(int direction)
    {
        const int index = historyIndex + direction;
        if (index < 0 || index >= (int)history.size())
            return;

        navigatingHistory = true;
        const int previous = historyIndex;
        historyIndex = index;
        NavigateTo(history[index]);
        navigatingHistory = false;

        // The folder is gone : stay where we were
        if (currentPath.full != history[index])
            historyIndex = previous;
    }

    void AssetBrowser::GoUp()
    {
        auto project = Engine::Core::GetEngine().GetCurrentProject();
        const Engine::Filesystem::Path root = project->GetProjectResourcesPath();
        if (currentPath.full == root.full)
            return;

        std::filesystem::path parent = std::filesystem::path(currentPath.full).parent_path();
        // Never above the project's resources
        if (parent.string().size() < root.full.size())
            return;
        NavigateTo(parent.string());
    }

    void AssetBrowser::MoveItemsInto(const std::vector<std::string>& namesInProject, const Engine::Filesystem::Path& folder)
    {
        namespace fs = std::filesystem;

        std::vector<std::string> failures;
        bool moved = false;

        for (const std::string& name : namesInProject)
        {
            // Dragged from this folder's listing
            auto found = std::find_if(items.begin(), items.end(), [&](const Asset& a) { return a.nameInProject == name; });
            if (found == items.end())
                continue;

            fs::path source(found->path.full);
            fs::path destination = fs::path(folder.full) / source.filename();

            std::error_code ec;
            if (fs::equivalent(source, fs::path(folder.full), ec)                        // a folder onto itself
                || fs::equivalent(source.parent_path(), fs::path(folder.full), ec)       // already there
                || fs::path(folder.full).string().rfind(source.string() + "/", 0) == 0)  // a folder into its own child
                continue;

            AssetOperations::Result r = AssetOperations::Move(found->path, Engine::Filesystem::Path(destination.string()));
            if (!r.ok)
                failures.push_back(r.message);
            else
                moved = true;
        }

        if (moved)
        {
            selection.Clear();
            dirty = true; navigationOnly = false;
        }

        ReportFailures("Move", failures);
    }

    std::string AssetBrowser::GetSelectedAssetName() const
    {
        if (selection.Size != 1)
            return "";

        for (const Asset& item : items)
        {
            if (selection.Contains(item.id))
                return item.isDirectory ? "" : item.nameInProject;
        }
        return "";
    }

    void AssetBrowser::RevealAsset(const std::string& nameInProject)
    {
        auto* assetIDs = Engine::Core::GetEngine().GetAssetIDManager();
        auto info = assetIDs->GetAssetFromID(assetIDs->GetIDFromNameInProject(nameInProject));
        if (!info || info->baseInfos.nameInProject != nameInProject)
            return;

        const Engine::Filesystem::Path root = Engine::Core::GetEngine().GetCurrentProject()->GetProjectResourcesPath();
        const std::string full = info->baseInfos.path.full;

        // Engine resources live outside the project, there is no folder of the browser to show them in
        if (full.rfind(root.full, 0) != 0)
        {
            Notifications::Info("\"%s\" is an engine resource, it is not in the project's content", nameInProject.c_str());
            return;
        }

        std::filesystem::path folder = std::filesystem::path(full).parent_path();
        searchBuffer[0] = '\0'; // a search could hide it

        NavigateTo(folder.string());
        pendingRevealPath = full;
        RequestRelist();

        ImGui::SetWindowFocus("Asset Browser");
    }
}