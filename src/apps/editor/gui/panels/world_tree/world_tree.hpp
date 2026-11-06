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

    class WorldTree
    {
        public:
            void Draw();

            void SetParentWindow(Core::EditorMainWindow* parent);

            void BeginRename(const std::shared_ptr<Engine::Objects::Actor>& actor);
            void BeginRenameFolder(const std::string& path);

        private:
            using ActorPtr = std::shared_ptr<Engine::Objects::Actor>;

            struct Row {
                bool isFolder = false;
                int depth = 0;
                ActorPtr actor;
                std::string path;
                std::string label;
                bool hasChildren = false;
                bool open = false;
                bool dimmed = false;
                int count = 0;
            };

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

            void BuildRows(Engine::Worlds::World* world);
            bool EmitFolderContents(const std::string& folder, int depth, bool filtering);
            bool EmitActor(const ActorPtr& actor, int depth, bool filtering);
            void SortActors(std::vector<ActorPtr>& actors) const;
            bool HasActiveFilter() const;
            bool MatchesFilter(const ActorPtr& actor) const;
            std::set<std::string> CollectFolders(Engine::Worlds::World* world) const;
            int IndexOfActorRow(int actorId) const;
            int IndexOfFolderRow(const std::string& path) const;

            void ActivateRow(int index, bool ctrl, bool shift);
            void SelectRange(int index, bool additive);
            void MoveCursor(int delta, bool extend);
            void RevealActor(const ActorPtr& actor);
            void UpdateReveal();
            void ExpandAll(Engine::Worlds::World* world);
            void CollapseAll(Engine::Worlds::World* world);
            std::vector<ActorPtr> SelectedTopLevel() const;

            void ReparentSelection(const std::vector<ActorPtr>& actors, const ActorPtr& newParent);
            void SpawnAssets(Engine::Worlds::World* world, const std::vector<std::string>& assets, const std::string& folder);
            void NewFolder(bool withSelection);
            void DeleteFolder(const std::string& path);

            void DrawRenameBox(float width);
            void CommitRename();
            void CancelRename() { renameKind = RenameKind::None; }

            Core::EditorMainWindow* parent = nullptr;

            std::vector<Row> rows;
            std::map<std::string, std::vector<ActorPtr>> buildActorsByFolder;
            std::map<std::string, std::vector<std::string>> buildSubfolders;
            std::map<std::string, int> buildFolderCounts;

            std::unordered_set<int> openActors;
            std::set<std::string> closedFolders;
            Engine::Worlds::World* knownWorld = nullptr;

            char searchBuffer[128] = "";
            bool focusSearch = false;
            unsigned categoryMask = 0x1FF;
            std::string queryLower;
            bool onlySelected = false;

            int sortColumn = -1;
            bool sortAscending = true;

            int anchorId = -1;
            std::string selectedFolder;
            int lastPrimaryId = -1;
            bool selectionTouched = false;
            int scrollToActorId = -1;
            std::string scrollToFolder;
            int hoveredRow = -1;
            int scrollRowIndex = -1;

            enum class RenameKind { None, Actor, Folder };
            RenameKind renameKind = RenameKind::None;
            int renameActorId = -1;
            std::string renameFolder;
            char renameBuffer[256] = "";
            bool renameNeedsFocus = false;
            int renameStartFrame = 0;

            std::function<void()> pendingEdit;
    };


}
