#pragma once

#include "engine/core/engine.hpp"

#include "editor/gui/resources/editor_resources.hpp"

#include "editor/gui/panels/common.hpp"

namespace Shard::Editor::Core{
    class EditorMainWindow;
}

namespace Shard::Editor::GUI{

    struct ExampleSelectionWithDeletion : ImGuiSelectionBasicStorage
    {
        // Find which item should be Focused after deletion.
        // Call _before_ item submission. Return an index in the before-deletion item list, your item loop should call SetKeyboardFocusHere() on it.
        // The subsequent ApplyDeletionPostLoop() code will use it to apply Selection.
        // - We cannot provide this logic in core Dear ImGui because we don't have access to selection data.
        // - We don't actually manipulate the ImVector<> here, only in ApplyDeletionPostLoop(), but using similar API for consistency and flexibility.
        // - Important: Deletion only works if the underlying ImGuiID for your items are stable: aka not depend on their index, but on e.g. item id/ptr.
        // FIXME-MULTISELECT: Doesn't take account of the possibility focus target will be moved during deletion. Need refocus or scroll offset.
        int ApplyDeletionPreLoop(ImGuiMultiSelectIO* ms_io, int items_count)
        {
            if (Size == 0)
                return -1;

            // If focused item is not selected...
            const int focused_idx = (int)ms_io->NavIdItem;  // Index of currently focused item
            if (ms_io->NavIdSelected == false)  // This is merely a shortcut, == Contains(adapter->IndexToStorage(items, focused_idx))
            {
                ms_io->RangeSrcReset = true;    // Request to recover RangeSrc from NavId next frame. Would be ok to reset even when NavIdSelected==true, but it would take an extra frame to recover RangeSrc when deleting a selected item.
                return focused_idx;             // Request to focus same item after deletion.
            }

            // If focused item is selected: land on first unselected item after focused item.
            for (int idx = focused_idx + 1; idx < items_count; idx++)
                if (!Contains(GetStorageIdFromIndex(idx)))
                    return idx;

            // If focused item is selected: otherwise return last unselected item before focused item.
            for (int idx = IM_MIN(focused_idx, items_count) - 1; idx >= 0; idx--)
                if (!Contains(GetStorageIdFromIndex(idx)))
                    return idx;

            return -1;
        }

        // Rewrite item list (delete items) + update selection.
        // - Call after EndMultiSelect()
        // - We cannot provide this logic in core Dear ImGui because we don't have access to your items, nor to selection data.
        template<typename ITEM_TYPE>
        void ApplyDeletionPostLoop(ImGuiMultiSelectIO* ms_io, ImVector<ITEM_TYPE>& items, int item_curr_idx_to_select)
        {
            // Rewrite item list (delete items) + convert old selection index (before deletion) to new selection index (after selection).
            // If NavId was not part of selection, we will stay on same item.
            ImVector<ITEM_TYPE> new_items;
            new_items.reserve(items.Size - Size);
            int item_next_idx_to_select = -1;
            for (int idx = 0; idx < items.Size; idx++)
            {
                if (!Contains(GetStorageIdFromIndex(idx)))
                    new_items.push_back(items[idx]);
                if (item_curr_idx_to_select == idx)
                    item_next_idx_to_select = new_items.Size - 1;
            }
            items.swap(new_items);

            // Update selection
            Clear();
            if (item_next_idx_to_select != -1 && ms_io->NavIdSelected)
                SetItemSelected(GetStorageIdFromIndex(item_next_idx_to_select), true);
        }
    };

    struct Asset{
        ImGuiID id;

        Engine::Filesystem::Path path;
        Engine::Filesystem::Type type;
        bool isDirectory;

        std::string nameInProject;
        int size = 0;

        const AtlasRegion* icon;  // cached pointer

        Asset(ImGuiID id, const Engine::Filesystem::Path& path, const Engine::Filesystem::Type type, bool isDir, const AtlasRegion* icon, const std::string& nameInProject = "", int size = 0)
        {
            this->path = path;
            this->type = type;
            this->id = id;
            this->icon = icon;
            this->isDirectory = isDir;
            this->nameInProject = nameInProject;
            this->size = size;
        }

        static const ImGuiTableSortSpecs* s_current_sort_specs;

        static void SortWithSortSpecs(ImGuiTableSortSpecs* sort_specs, Asset* items, int items_count)
        {
            s_current_sort_specs = sort_specs;

            if (items_count > 1)
            {
                std::sort(items, items + items_count,
                    [](const Asset& a, const Asset& b)
                    {
                        for (int n = 0; n < s_current_sort_specs->SpecsCount; n++)
                        {
                            const ImGuiTableColumnSortSpecs* sort_spec = &s_current_sort_specs->Specs[n];
                            int delta = 0;

                            if (sort_spec->ColumnIndex == 0)
                                delta = (int)a.id - (int)b.id;
                            else if (sort_spec->ColumnIndex == 1)
                                delta = a.path.full.compare(b.path.full);
                            if (delta > 0)
                                return sort_spec->SortDirection == ImGuiSortDirection_Ascending;
                            if (delta < 0)
                                return sort_spec->SortDirection == ImGuiSortDirection_Descending;
                        }

                        return a.id < b.id;
                    });
            }

            s_current_sort_specs = nullptr;
        }

        static int IMGUI_CDECL CompareWithSortSpecs(const void* lhs, const void* rhs)
        {
            const Asset* a = (const Asset*)lhs;
            const Asset* b = (const Asset*)rhs;
            for (int n = 0; n < s_current_sort_specs->SpecsCount; n++)
            {
                const ImGuiTableColumnSortSpecs* sort_spec = &s_current_sort_specs->Specs[n];
                int delta = 0;
                if (sort_spec->ColumnIndex == 0)
                    delta = ((int)a->id - (int)b->id);
                else if (sort_spec->ColumnIndex == 1)
                    delta = a->path.full.compare(b->path.full);
                if (delta > 0)
                    return (sort_spec->SortDirection == ImGuiSortDirection_Ascending) ? +1 : -1;
                if (delta < 0)
                    return (sort_spec->SortDirection == ImGuiSortDirection_Ascending) ? -1 : +1;
            }
            return ((int)a->id - (int)b->id);
        }
    };
    
    class AssetBrowser
    {
        public:
            void Refresh();
            void Draw();
            void NavigateTo(const std::string& path);
            void SetParentWindow(Core::EditorMainWindow* parent);

        private:
            void DrawBreadcrumb();
            void DrawAssets();
            void UpdateLayoutSizes(float avail_width);
            void RequestOpenLevel(const Engine::Filesystem::Path &path);

            // Context menu / shortcuts only *request* an action (pendingAction); it runs once per frame
            // after the item loop (ProcessAction), since most of them rebuild `items`.
            enum class Action { None, Open, Cut, Copy, Paste, Duplicate, Rename, Delete, Refresh, NewFolder, NewMaterial, NewLevel, Reveal, CopyPath };

            void DrawContextMenu();
            void ProcessAction();
            void DrawDialogs();
            void OpenItem(const Asset& item);
            std::vector<const Asset*> GetSelectedItems() const;
            const Asset* FindItem(ImGuiID id) const;

            void BeginRename(const Engine::Filesystem::Path& path);
            void ApplyRename();
            void ConfirmDelete(const std::vector<Engine::Filesystem::Path>& paths);
            void ReportFailures(const std::string& title, const std::vector<std::string>& failures);

            Core::EditorMainWindow* parent = nullptr;

            Engine::Filesystem::Path currentPath;
            float thumbnailSize = 72.f;

            Action pendingAction = Action::None;
            ImGuiID contextTargetId = 0;                    // item that was right-clicked (0 = empty space)
            Engine::Filesystem::Path pasteTargetDir;        // folder to paste into (empty = the current folder)

            std::vector<Engine::Filesystem::Path> clipboard;
            bool clipboardCut = false;

            Engine::Filesystem::Path renamePath;
            char renameBuffer[256]{};
            bool openRenamePopup = false;

            std::vector<Asset> items;               // Our items
            ExampleSelectionWithDeletion selection;     // Our selection (ImGuiSelectionBasicStorage + helper funcs to handle deletion)
            ImGuiID         nextItemId = 0;             // Unique identifier when creating new items
            bool            requestSort = false;        // Deferred sort request
            float           zoomWheelAccum = 0.0f;      // Mouse wheel accumulator to handle smooth wheels better

            bool dirty = true;

            std::vector<Engine::Filesystem::Type> allowedTypes = {
                Engine::Filesystem::Type::T_IMAGE,
                Engine::Filesystem::Type::T_SOUND,
                Engine::Filesystem::Type::T_FONT,
                Engine::Filesystem::Type::T_SHADER,
                Engine::Filesystem::Type::T_COMPUTE_SHADER,
                Engine::Filesystem::Type::T_TEXT,
                Engine::Filesystem::Type::T_SCRIPT,
                Engine::Filesystem::Type::T_LEVEL,
                Engine::Filesystem::Type::T_MODEL,
                Engine::Filesystem::Type::T_MATERIAL,
                Engine::Filesystem::Type::T_CONFIG,
                Engine::Filesystem::Type::T_DIRECTORY
            };

            bool            AllowSorting = true;
            bool            AllowDragUnselected = false;
            bool            AllowBoxSelect = true;
            ImVec2 ItemSize        = ImVec2(90.0f, 138.0f);  // Full clickable/selectable area
            ImVec2 ThumbnailSize   = ImVec2(80.0f, 80.0f);    // Actual image size
            ImVec2 Spacing         = ImVec2(20.0f, 20.0f);    // Horizontal / Vertical spacing
            float IconHitSpacing   = 6;
            float IconTopPadding   = 8.0f;    // Fixed gap between the tile top and the thumbnail
            bool            StretchSpacing = true;

            ImVec2          LayoutItemSize;
            ImVec2          LayoutItemStep;             // == LayoutItemSize + LayoutItemSpacing
            float           LayoutItemSpacing = 0.0f;
            float           LayoutSelectableSpacing = 0.0f;
            float           LayoutOuterPadding = 0.0f;
            int             LayoutColumnCount = 0;
            int             LayoutLineCount = 0;
    };
}