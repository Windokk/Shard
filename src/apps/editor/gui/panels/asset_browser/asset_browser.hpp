#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "engine/world/engine.hpp"

#include "apps/editor/gui/resources/editor_resources.hpp"

#include "apps/editor/gui/panels/common.hpp"

namespace Shard::Editor::Core{
    class EditorMainWindow;
}

namespace Shard::Editor::GUI{

    struct ExampleSelectionWithDeletion : ImGuiSelectionBasicStorage
    {
        int ApplyDeletionPreLoop(ImGuiMultiSelectIO* ms_io, int items_count)
        {
            if (Size == 0)
                return -1;

            const int focused_idx = (int)ms_io->NavIdItem;
            if (ms_io->NavIdSelected == false)
            {
                ms_io->RangeSrcReset = true;
                return focused_idx;
            }

            for (int idx = focused_idx + 1; idx < items_count; idx++)
                if (!Contains(GetStorageIdFromIndex(idx)))
                    return idx;

            for (int idx = IM_MIN(focused_idx, items_count) - 1; idx >= 0; idx--)
                if (!Contains(GetStorageIdFromIndex(idx)))
                    return idx;

            return -1;
        }

        template<typename ITEM_TYPE>
        void ApplyDeletionPostLoop(ImGuiMultiSelectIO* ms_io, ImVector<ITEM_TYPE>& items, int item_curr_idx_to_select)
        {
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

        std::string filename;
        std::string lowerName;
        std::string line1, line2;
        float wrapWidth = -1.0f;

        const AtlasRegion* icon;

        Asset(ImGuiID id, const Engine::Filesystem::Path& path, const Engine::Filesystem::Type type, bool isDir, const AtlasRegion* icon, const std::string& nameInProject = "", int size = 0)
        {
            this->path = path;
            this->type = type;
            this->id = id;
            this->icon = icon;
            this->isDirectory = isDir;
            this->nameInProject = nameInProject;
            this->size = size;
            this->filename = path.GetFilename();
            this->lowerName = filename;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
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

            std::string GetSelectedAssetName() const;
            void RevealAsset(const std::string& nameInProject);

        private:
            enum class SortKey { Name, Type, Size };

            struct FolderNode {
                std::string name;
                std::string path;
                std::vector<FolderNode> children;
            };

            void DrawSidebar(float height);
            void DrawFolderNode(const FolderNode& node, bool isRoot);
            void RebuildFolderTree();
            void DrawFilterPopup();
            void DrawViewPopup();
            void SetZoom(float size);
            int ActiveFilterCount() const;
            void RequestRelist() { if (!dirty) navigationOnly = true; dirty = true; }
            void NavigateHistory(int direction);
            void GoUp();
            bool PassesFilter(const Asset& item) const;
            void MoveItemsInto(const std::vector<std::string>& namesInProject, const Engine::Filesystem::Path& folder);

            void DrawBreadcrumb();
            void DrawAssets();
            void UpdateLayoutSizes(float avail_width);
            void RequestOpenWorld(const Engine::Filesystem::Path &path);

            // Menu and shortcuts only request an action, it runs after the item loop (ProcessAction) since most rebuild `items`.
            enum class Action { None, Open, Cut, Copy, Paste, Duplicate, Rename, Delete, Refresh, NewFolder, NewMaterial, NewWorld, Reveal, CopyPath, RefreshThumbnail, ShowInFolder, Import };

            void DrawContextMenu();
            void ProcessAction();
            void OpenImportDialog();
            void ProcessImports();
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
            float thumbnailSize = 80.f;

            Action pendingAction = Action::None;
            ImGuiID contextTargetId = 0;
            Engine::Filesystem::Path pasteTargetDir;

            std::vector<Engine::Filesystem::Path> clipboard;
            bool clipboardCut = false;

            // The file dialog answers from its own thread. Shared with its callback, which may outlive the browser.
            struct ImportRequest {
                Engine::Filesystem::Path destination;
                std::vector<std::string> sources;
            };
            struct ImportQueue {
                std::mutex mutex;
                std::vector<ImportRequest> requests;
                std::string lastFolder;
                std::atomic<bool> dialogOpen{false};
            };
            std::shared_ptr<ImportQueue> importQueue = std::make_shared<ImportQueue>();

            Engine::Filesystem::Path renamePath;
            char renameBuffer[256]{};
            bool openRenamePopup = false;

            std::vector<std::string> history;
            int historyIndex = -1;
            bool navigatingHistory = false;

            char searchBuffer[128]{};
            bool focusSearch = false;

            std::string pendingRevealPath;
            int pendingScrollItem = -1;

            int visibleItemCount = 0;
            SortKey sortKey = SortKey::Name;
            bool sortAscending = true;
            bool listView = false;
            bool showSidebar = true;
            float sidebarWidth = 200.0f;

            bool typeHidden[14]{};
            bool showFolders = true;
            bool searchSubfolders = true;

            FolderNode folderTree;
            bool treeDirty = true;
            bool treeRevealCurrent = false;
            bool navigationOnly = false;

            std::vector<Asset> items;
            ExampleSelectionWithDeletion selection;
            ImGuiID         nextItemId = 0;
            bool            requestSort = false;
            float           zoomWheelAccum = 0.0f;

            bool dirty = true;
            std::shared_ptr<std::vector<std::string>> staleThumbnails = std::make_shared<std::vector<std::string>>();
            bool hotReloadListening = false;
            uint64_t hotReloadRevision = 0;

            std::vector<Engine::Filesystem::Type> allowedTypes = {
                Engine::Filesystem::Type::T_IMAGE,
                Engine::Filesystem::Type::T_SOUND,
                Engine::Filesystem::Type::T_FONT,
                Engine::Filesystem::Type::T_SHADER,
                Engine::Filesystem::Type::T_COMPUTE_SHADER,
                Engine::Filesystem::Type::T_TEXT,
                Engine::Filesystem::Type::T_SCRIPT,
                Engine::Filesystem::Type::T_WORLD,
                Engine::Filesystem::Type::T_MODEL,
                Engine::Filesystem::Type::T_MATERIAL,
                Engine::Filesystem::Type::T_PROBES,
                Engine::Filesystem::Type::T_LIGHTMAP,
                Engine::Filesystem::Type::T_CONFIG,
                Engine::Filesystem::Type::T_DIRECTORY
            };

            bool            AllowSorting = true;
            bool            AllowDragUnselected = false;
            bool            AllowBoxSelect = true;
            ImVec2 ItemSize        = ImVec2(90.0f, 138.0f);
            ImVec2 ThumbnailSize   = ImVec2(80.0f, 80.0f);
            ImVec2 Spacing         = ImVec2(20.0f, 20.0f);
            float IconHitSpacing   = 6;
            float IconTopPadding   = 8.0f;
            bool            StretchSpacing = true;

            ImVec2          LayoutItemSize;
            ImVec2          LayoutItemStep;
            float           LayoutItemSpacing = 0.0f;
            float           LayoutSelectableSpacing = 0.0f;
            float           LayoutOuterPadding = 0.0f;
            int             LayoutColumnCount = 0;
            int             LayoutLineCount = 0;
    };
}