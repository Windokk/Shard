#pragma once

#include "engine/world/world.hpp"
#include "engine/world/actor.hpp"

#include "imgui/imgui.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <typeinfo>
#include <unordered_set>
#include <vector>

namespace Shard::Editor::Core{

    class EditorMainWindow;
}

namespace Shard::Editor::GUI{

    /// @brief The outliner : the actors of the world as a tree (with the folders that organize its root actors), in a table
    /// whose columns can be shown, hidden, moved and sorted. The selection it edits is the editor's (EditorMainWindow), shared
    /// with the viewport.
    ///
    /// Rows : click selects, Ctrl+click adds / removes, Shift+click selects the range from the last click. Drag rows onto an
    /// actor to put them under it, onto a folder to put them in it. Shortcuts (the panel focused) : F2 rename, Delete, Ctrl+C / X
    /// / V / D / A / G, Ctrl+Shift+N new folder, Ctrl+F search, Escape deselect, arrows to move around the tree.
    class WorldTree
    {
        public:
            void Draw();

            void SetParentWindow(Core::EditorMainWindow* parent);

            /// Renames the actor in place, in its row (the tree scrolls to it and opens what hides it)
            void BeginRename(const std::shared_ptr<Engine::Objects::Actor>& actor);
            /// Same for a folder (its path)
            void BeginRenameFolder(const std::string& path);

        private:
            using ActorPtr = std::shared_ptr<Engine::Objects::Actor>;

            /// A line of the tree as it is shown (rebuilt every frame : the folders, the open actors and the filters decide which)
            struct Row {
                bool isFolder = false;
                int depth = 0;
                ActorPtr actor;                 // actor rows
                std::string path;               // folder rows : the whole path
                std::string label;              // folder rows : its last part
                bool hasChildren = false;
                bool open = false;
                bool dimmed = false;            // shown only because something under it matches the filters
                int count = 0;                  // folder rows : the actors directly in it
            };

            // Which column is which (ImGui's column user ids)
            enum Column { ColumnName = 1, ColumnType, ColumnComponents, ColumnChildren, ColumnId };

            void DrawToolbar(Engine::Worlds::World* world);
            void DrawFilterPopup();
            void DrawCreateMenu(const ActorPtr& parentActor, const std::string& folder);
            void DrawTable(Engine::Worlds::World* world);
            void DrawRow(int index);
            void DrawActorContextMenu(const ActorPtr& actor);
            void DrawFolderContextMenu(const std::string& path);
            void DrawEmptyContextMenu(Engine::Worlds::World* world);
            void DrawMoveToFolderMenu();
            void HandleShortcuts();
            void HandleWindowDrop(Engine::Worlds::World* world, const ImVec2& min, const ImVec2& max);

            // The rows
            void BuildRows(Engine::Worlds::World* world);
            bool EmitFolderContents(const std::string& folder, int depth, bool filtering);
            bool EmitActor(const ActorPtr& actor, int depth, bool filtering);
            void SortActors(std::vector<ActorPtr>& actors) const;
            bool HasActiveFilter() const;
            bool MatchesFilter(const ActorPtr& actor) const;
            std::set<std::string> CollectFolders(Engine::Worlds::World* world) const;
            int IndexOfActorRow(int actorId) const;
            int IndexOfFolderRow(const std::string& path) const;

            // Selection
            void ActivateRow(int index, bool ctrl, bool shift);
            void SelectRange(int index, bool additive);
            void MoveCursor(int delta, bool extend);
            void RevealActor(const ActorPtr& actor);
            void UpdateReveal();
            void ExpandAll(Engine::Worlds::World* world);
            void CollapseAll(Engine::Worlds::World* world);
            /// The selected actors that have no selected ancestor (the ones a drag or a delete works on)
            std::vector<ActorPtr> SelectedTopLevel() const;

            // Edits
            void ReparentSelection(const std::vector<ActorPtr>& actors, const ActorPtr& newParent);
            void SpawnAssets(Engine::Worlds::World* world, const std::vector<std::string>& assets, const std::string& folder);
            void NewFolder(bool withSelection);
            void DeleteFolder(const std::string& path);

            // Rename in place
            void DrawRenameBox(float width);
            void CommitRename();
            void CancelRename() { renameKind = RenameKind::None; }

            Core::EditorMainWindow* parent = nullptr;

            std::vector<Row> rows;
            // While the rows are built
            std::map<std::string, std::vector<ActorPtr>> buildActorsByFolder;
            std::map<std::string, std::vector<std::string>> buildSubfolders;
            std::map<std::string, int> buildFolderCounts;

            // What is open : actors are closed unless listed, folders are open unless listed
            std::unordered_set<int> openActors;
            std::set<std::string> closedFolders;
            Engine::Worlds::World* knownWorld = nullptr;

            // The filters
            char searchBuffer[128] = "";
            bool focusSearch = false;
            unsigned categoryMask = 0x1FF;      // the categories shown (bits : see the .cpp), all of them by default
            std::string queryLower;             // the search, in lower case, while the rows are built
            bool onlySelected = false;

            // The sort of the siblings (-1 : the order they were made in)
            int sortColumn = -1;
            bool sortAscending = true;

            // Selection
            int anchorId = -1;                  // the actor the Shift+click ranges start from
            std::string selectedFolder;
            int lastPrimaryId = -1;
            bool selectionTouched = false;
            int scrollToActorId = -1;
            std::string scrollToFolder;
            int hoveredRow = -1;
            int scrollRowIndex = -1;            // the row the table scrolls to this frame

            // Rename in place
            enum class RenameKind { None, Actor, Folder };
            RenameKind renameKind = RenameKind::None;
            int renameActorId = -1;
            std::string renameFolder;
            char renameBuffer[256] = "";
            bool renameNeedsFocus = false;
            int renameStartFrame = 0;

            // An edit asked for while the rows are being drawn, run once they are : destroying or moving an actor changes the lists
            // being walked
            std::function<void()> pendingEdit;
    };


}
