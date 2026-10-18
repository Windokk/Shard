#include "world_tree.hpp"

#include "engine/world/engine.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"

#include "engine/audio/audio_source.hpp"
#include "engine/audio/audio_listener.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/renderer/components/camera.hpp"
#include "engine/renderer/components/light_component.hpp"
#include "engine/renderer/components/model_component.hpp"
#include "engine/renderer/components/probe_volume.hpp"
#include "engine/renderer/components/reflection_probe.hpp"

#include "apps/editor/gui/main_window.hpp"
#include "apps/editor/gui/IconsLucide.h"
#include "apps/editor/gui/notifications.hpp"
#include "apps/editor/gui/dragdrop/asset_drag_drop.hpp"
#include "apps/editor/commands/actor_commands.hpp"
#include "apps/editor/commands/command_stack.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace Shard::Editor::GUI{

    using Engine::Core::GetEngine;

    namespace {

        // Payload of a drag of rows : the ids of the actors (the top ones of the selection)
        constexpr const char* kActorsPayload = "SHARD_OUTLINER_ACTORS";
        // Payload of a drag of a folder row : its path
        constexpr const char* kFolderPayload = "SHARD_OUTLINER_FOLDER";

        constexpr float kIndent = 16.0f;

        // What an actor has, as bits (the filters)
        enum : unsigned {
            CategoryModel   = 1 << 0,
            CategoryLight   = 1 << 1,
            CategoryCamera  = 1 << 2,
            CategoryAudio   = 1 << 3,
            CategoryPhysics = 1 << 4,
            CategoryProbe   = 1 << 5,
            CategoryScript  = 1 << 6,
            CategoryOther   = 1 << 7,
            CategoryEmpty   = 1 << 8,
            CategoryAll     = (1 << 9) - 1
        };

        using ActorPtr = std::shared_ptr<Engine::Objects::Actor>;

        std::string Lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            return text;
        }

        // "PhysicsBody" -> "Physics Body"
        std::string PrettyName(const std::string& name)
        {
            std::string result;
            for (size_t i = 0; i < name.size(); ++i)
            {
                if (i > 0 && std::isupper(static_cast<unsigned char>(name[i])) && !std::isupper(static_cast<unsigned char>(name[i - 1])))
                    result += ' ';
                result += name[i];
            }
            return result;
        }

        std::string LeafOf(const std::string& path)
        {
            const size_t slash = path.rfind('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        std::string ParentPathOf(const std::string& path)
        {
            const size_t slash = path.rfind('/');
            return slash == std::string::npos ? std::string() : path.substr(0, slash);
        }

        ActorPtr ParentActorOf(const ActorPtr& actor)
        {
            return actor ? std::dynamic_pointer_cast<Engine::Objects::Actor>(actor->GetParent()) : nullptr;
        }

        std::vector<ActorPtr> ChildrenOf(const ActorPtr& actor)
        {
            std::vector<ActorPtr> children;
            for (Engine::Core::ObjectID childID : actor->GetChildrenID(false))
                if (auto child = std::dynamic_pointer_cast<Engine::Objects::Actor>(actor->GetChild(childID)))
                    children.push_back(child);
            return children;
        }

        ActorPtr ActorFromId(int id)
        {
            return std::dynamic_pointer_cast<Engine::Objects::Actor>(
                GetEngine().GetObjectIDManager()->GetObjectFromID(Engine::Core::ObjectID(id)));
        }

        // The components that say what an actor is (everything but its transform)
        std::vector<std::string> ComponentNames(const ActorPtr& actor)
        {
            std::vector<std::string> names;
            for (const auto& component : actor->GetComponents())
            {
                const ClassDescriptor* descriptor = component->GetDescriptor();
                if (!descriptor || descriptor->name == "Transform")
                    continue;
                names.push_back(PrettyName(descriptor->name));
            }
            return names;
        }

        unsigned CategoriesOf(const ActorPtr& actor)
        {
            unsigned bits = 0;
            bool any = false;

            for (const auto& component : actor->GetComponents())
            {
                const ClassDescriptor* descriptor = component->GetDescriptor();
                const std::string name = descriptor ? descriptor->name : std::string();
                if (name == "Transform")
                    continue;

                any = true;
                if (dynamic_cast<Engine::Objects::Components::Script*>(component.get()))
                    bits |= CategoryScript;
                else if (name == "Model")
                    bits |= CategoryModel;
                else if (name == "Light")
                    bits |= CategoryLight;
                else if (name == "Camera")
                    bits |= CategoryCamera;
                else if (name.rfind("Audio", 0) == 0)
                    bits |= CategoryAudio;
                else if (name == "PhysicsBody" || name.find("Gravity") != std::string::npos)
                    bits |= CategoryPhysics;
                else if (name.find("Probe") != std::string::npos)
                    bits |= CategoryProbe;
                else
                    bits |= CategoryOther;
            }

            return any ? bits : static_cast<unsigned>(CategoryEmpty);
        }

        // What the Type column says : the first component that is not the transform, "+N" for the others
        std::string TypeOf(const ActorPtr& actor)
        {
            const std::vector<std::string> names = ComponentNames(actor);
            if (names.empty())
                return "Actor";
            return names.size() == 1 ? names[0] : names[0] + " +" + std::to_string(names.size() - 1);
        }

        struct IconInfo {
            const char* icon;
            ImU32 color;
        };

        IconInfo IconOf(const ActorPtr& actor)
        {
            const unsigned categories = CategoriesOf(actor);
            if (categories & CategoryModel)   return { ICON_LC_BOX,        IM_COL32(140, 190, 255, 255) };
            if (categories & CategoryLight)   return { ICON_LC_LIGHTBULB,  IM_COL32(255, 217,  89, 255) };
            if (categories & CategoryCamera)  return { ICON_LC_CAMERA,     IM_COL32(153, 230, 153, 255) };
            if (categories & CategoryAudio)   return { ICON_LC_VOLUME_2,   IM_COL32(230, 153, 230, 255) };
            if (categories & CategoryPhysics) return { ICON_LC_ATOM,       IM_COL32(255, 153, 102, 255) };
            if (categories & CategoryProbe)   return { ICON_LC_SCAN,       IM_COL32(128, 230, 230, 255) };
            if (categories & CategoryScript)  return { ICON_LC_CODE,       IM_COL32(204, 204, 128, 255) };
            if (categories & CategoryOther)   return { ICON_LC_COMPONENT,  IM_COL32(180, 180, 180, 255) };
            return { ICON_LC_CIRCLE_DOT, IM_COL32(150, 150, 150, 255) };
        }

        constexpr ImU32 kFolderColor = IM_COL32(242, 199, 89, 255);

        void SetSubtreeOpen(const ActorPtr& actor, bool open, std::unordered_set<int>& openActors)
        {
            for (const auto& child : ChildrenOf(actor))
                SetSubtreeOpen(child, open, openActors);

            if (open)
                openActors.insert(actor->GetID().GetAsInt());
            else
                openActors.erase(actor->GetID().GetAsInt());
        }
    }

    void WorldTree::Draw()
    {
        auto world = GetEngine().GetWorldManager()->GetWorldAt(0);

        // Stable ImGui ID (###WorldTree) so the title can carry the world's name (and an unsaved-
        // changes marker) without losing this panel's saved dock position/size every time the world
        // name changes or a different world is loaded.
        std::string title = "World";
        if (world)
        {
            title += " - " + world->GetName();
            if (world->IsDirty())
                title += "*";
        }
        title += "###WorldTree";

        if (!ImGui::Begin(title.c_str()))
        {
            ImGui::End();
            return;
        }

        if (!world)
        {
            ImGui::Text("No world loaded");
            ImGui::End();
            return;
        }

        // Another world : nothing that was open, selected or being renamed is there anymore
        if (world != knownWorld)
        {
            knownWorld = world;
            openActors.clear();
            closedFolders.clear();
            selectedFolder.clear();
            scrollToFolder.clear();
            CancelRename();
            lastPrimaryId = -1;
            anchorId = -1;
            scrollToActorId = -1;
        }

        DrawToolbar(world);

        UpdateReveal();
        BuildRows(world);
        DrawTable(world);

        HandleShortcuts();

        // Run once the rows are drawn : destroying or moving an actor changes the lists that were walked
        if (pendingEdit)
        {
            std::function<void()> edit = std::move(pendingEdit);
            pendingEdit = nullptr;
            edit();
        }

        ImGui::End();
    }

    void WorldTree::SetParentWindow(Core::EditorMainWindow *parent)
    {
        this->parent = parent;
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Toolbar
    // ---------------------------------------------------------------------------------------------------------------------

    void WorldTree::DrawToolbar(Engine::Worlds::World* world)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float button = ImGui::GetFrameHeight();
        const float searchWidth = ImGui::GetContentRegionAvail().x - 3.0f * (button + style.ItemSpacing.x);

        ImGui::SetNextItemWidth(std::max(searchWidth, 60.0f));
        if (focusSearch)
        {
            ImGui::SetKeyboardFocusHere();
            focusSearch = false;
        }
        ImGui::InputTextWithHint("##OutlinerSearch", ICON_LC_SEARCH "  Search by name or type...", searchBuffer, sizeof(searchBuffer),
                                 ImGuiInputTextFlags_EscapeClearsAll);

        // Filters : lit when something is filtered out
        ImGui::SameLine();
        const bool filtered = categoryMask != CategoryAll || onlySelected;
        if (filtered)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(ICON_LC_FUNNEL "##OutlinerFilter", ImVec2(button, button)))
            ImGui::OpenPopup("##OutlinerFilterPopup");
        if (filtered)
            ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Filters");
        DrawFilterPopup();

        ImGui::SameLine();
        if (ImGui::Button(ICON_LC_PLUS "##OutlinerCreate", ImVec2(button, button)))
            ImGui::OpenPopup("##OutlinerCreatePopup");
        ImGui::SetItemTooltip("Create an actor");
        if (ImGui::BeginPopup("##OutlinerCreatePopup"))
        {
            DrawCreateMenu(nullptr, selectedFolder);
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(ICON_LC_FOLDER_PLUS "##OutlinerNewFolder", ImVec2(button, button)))
            NewFolder(parent && parent->HasSelection());
        ImGui::SetItemTooltip("New folder (the selected actors move into it)");

        (void)world;
    }

    void WorldTree::DrawFilterPopup()
    {
        if (!ImGui::BeginPopup("##OutlinerFilterPopup"))
            return;

        struct Entry { unsigned bit; const char* label; };
        static const Entry entries[] = {
            { CategoryModel,   "Models" },
            { CategoryLight,   "Lights" },
            { CategoryCamera,  "Cameras" },
            { CategoryAudio,   "Audio" },
            { CategoryPhysics, "Physics" },
            { CategoryProbe,   "Probes" },
            { CategoryScript,  "Scripts" },
            { CategoryOther,   "Other components" },
            { CategoryEmpty,   "Empty actors" },
        };

        ImGui::TextDisabled("Show");
        for (const Entry& entry : entries)
        {
            bool on = (categoryMask & entry.bit) != 0;
            if (ImGui::Checkbox(entry.label, &on))
            {
                if (on)
                    categoryMask |= entry.bit;
                else
                    categoryMask &= ~entry.bit;
            }
        }

        ImGui::Separator();
        ImGui::Checkbox("Selected only", &onlySelected);
        ImGui::Separator();

        if (ImGui::Button("Reset", ImVec2(-1.0f, 0.0f)))
        {
            categoryMask = CategoryAll;
            onlySelected = false;
            searchBuffer[0] = '\0';
        }

        ImGui::EndPopup();
    }

    void WorldTree::DrawCreateMenu(const ActorPtr& parentActor, const std::string& folder)
    {
        // One entry of the menu : an actor (with whatever `setup` adds to it) as one step of the undo history, then renamed in place
        auto create = [&](const char* label, const char* name, const char* undoLabel, std::function<void(const ActorPtr&)> setup)
        {
            if (!ImGui::MenuItem(label))
                return;

            pendingEdit = [this, parentActor, folder, name = std::string(name), undoLabel = std::string(undoLabel), setup]{
                auto& stack = Commands::CommandStack::Get();
                stack.Begin(undoLabel);
                ActorPtr actor = Commands::Actions::CreateActor(parentActor, name, folder);
                if (actor && setup)
                    setup(actor);
                stack.End();

                if (!actor)
                    return;

                if (parentActor)
                    openActors.insert(parentActor->GetID().GetAsInt());
                selectedFolder.clear();
                parent->SetSelectedActor(actor);
                anchorId = actor->GetID().GetAsInt();
                selectionTouched = true;
                BeginRename(actor);
            };
        };

        namespace C = Engine::Objects::Components;

        create("Empty Actor", "New Actor", "Create Actor", nullptr);
        ImGui::Separator();
        create("Light", "Light", "Create Light", [](const ActorPtr& a){ a->AddComponent<C::Light>(); });
        create("Camera", "Camera", "Create Camera", [](const ActorPtr& a){ a->AddComponent<C::Camera>(); });
        create("Audio Source", "Audio Source", "Create Audio Source", [](const ActorPtr& a){ a->AddComponent<C::AudioSource>(); });
        create("Physics Body", "Physics Body", "Create Physics Body", [](const ActorPtr& a){ a->AddComponent<C::PhysicsBody>(); });
        ImGui::Separator();
        create("Probe Volume", "Probe Volume", "Create Probe Volume", [](const ActorPtr& a){ a->AddComponent<C::ProbeVolume>(); });
        create("Reflection Probe", "Reflection Probe", "Create Reflection Probe", [](const ActorPtr& a){ a->AddComponent<C::ReflectionProbe>(); });
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Rows
    // ---------------------------------------------------------------------------------------------------------------------

    bool WorldTree::HasActiveFilter() const
    {
        return searchBuffer[0] != '\0' || categoryMask != CategoryAll || onlySelected;
    }

    bool WorldTree::MatchesFilter(const ActorPtr& actor) const
    {
        if (onlySelected && !parent->IsSelected(actor))
            return false;

        if (categoryMask != CategoryAll && (CategoriesOf(actor) & categoryMask) == 0)
            return false;

        if (!queryLower.empty())
        {
            if (Lower(actor->GetName()).find(queryLower) == std::string::npos &&
                Lower(TypeOf(actor)).find(queryLower) == std::string::npos)
                return false;
        }

        return true;
    }

    std::set<std::string> WorldTree::CollectFolders(Engine::Worlds::World* world) const
    {
        std::set<std::string> folders;

        for (const auto& f : world->GetFolders())
        {
            const std::string path = Commands::Actions::NormalizeFolderPath(f);
            if (!path.empty())
                folders.insert(path);
        }

        for (const auto& [id, actor] : world->GetRootActors())
        {
            if (!actor || actor->GetFolder().empty())
                continue;
            const std::string path = Commands::Actions::NormalizeFolderPath(actor->GetFolder());
            if (!path.empty())
                folders.insert(path);
        }

        // The folders around the ones that were made are there too
        const std::set<std::string> made = folders;
        for (std::string path : made)
            for (size_t slash = path.rfind('/'); slash != std::string::npos; slash = path.rfind('/'))
            {
                path = path.substr(0, slash);
                folders.insert(path);
            }

        return folders;
    }

    void WorldTree::SortActors(std::vector<ActorPtr>& actors) const
    {
        struct Keyed {
            std::string text;
            int number;
            int id;
            ActorPtr actor;
        };

        std::vector<Keyed> keyed;
        keyed.reserve(actors.size());
        for (const auto& actor : actors)
        {
            Keyed k;
            k.id = actor->GetID().GetAsInt();
            k.number = k.id;
            k.actor = actor;

            switch (sortColumn)
            {
                case ColumnName:     k.text = Lower(actor->GetName()); break;
                case ColumnType:     k.text = Lower(TypeOf(actor)) + "\n" + Lower(actor->GetName()); break;
                case ColumnChildren: k.number = actor->GetChildrenCount(); break;
                default: break;
            }
            keyed.push_back(std::move(k));
        }

        const bool ascending = sortColumn < 0 || sortAscending;
        const bool byText = sortColumn == ColumnName || sortColumn == ColumnType;

        std::stable_sort(keyed.begin(), keyed.end(), [&](const Keyed& a, const Keyed& b){
            if (byText)
            {
                if (a.text != b.text)
                    return ascending ? a.text < b.text : a.text > b.text;
            }
            else if (a.number != b.number)
            {
                return ascending ? a.number < b.number : a.number > b.number;
            }
            return a.id < b.id;
        });

        for (size_t i = 0; i < actors.size(); ++i)
            actors[i] = keyed[i].actor;
    }

    void WorldTree::BuildRows(Engine::Worlds::World* world)
    {
        rows.clear();
        buildActorsByFolder.clear();
        buildSubfolders.clear();
        buildFolderCounts.clear();

        queryLower = Lower(searchBuffer);
        const bool filtering = HasActiveFilter();

        std::vector<ActorPtr> roots;
        for (const auto& [id, actor] : world->GetRootActors())
            if (actor)
                roots.push_back(actor);

        // Without a sort, in the order they were made in (their ids)
        if (sortColumn < 0)
            std::sort(roots.begin(), roots.end(), [](const ActorPtr& a, const ActorPtr& b){ return a->GetID().GetAsInt() < b->GetID().GetAsInt(); });
        else
            SortActors(roots);

        for (const auto& actor : roots)
            buildActorsByFolder[Commands::Actions::NormalizeFolderPath(actor->GetFolder())].push_back(actor);

        const std::set<std::string> folders = CollectFolders(world);
        for (const auto& path : folders)
            buildSubfolders[ParentPathOf(path)].push_back(path);

        // A folder that was deleted (or its parent) while it was selected
        if (!selectedFolder.empty() && !folders.count(selectedFolder))
            selectedFolder.clear();

        EmitFolderContents(std::string(), 0, filtering);

        // The actor or folder being renamed is not there anymore (destroyed, filtered out...)
        if (renameKind == RenameKind::Actor && IndexOfActorRow(renameActorId) < 0)
            CancelRename();
        else if (renameKind == RenameKind::Folder && IndexOfFolderRow(renameFolder) < 0)
            CancelRename();
    }

    bool WorldTree::EmitFolderContents(const std::string& folder, int depth, bool filtering)
    {
        bool shown = false;

        auto subfolders = buildSubfolders.find(folder);
        if (subfolders != buildSubfolders.end())
        {
            for (const std::string& path : subfolders->second)
            {
                const size_t mark = rows.size();

                Row row;
                row.isFolder = true;
                row.depth = depth;
                row.path = path;
                row.label = LeafOf(path);

                auto inside = buildActorsByFolder.find(path);
                row.count = inside == buildActorsByFolder.end() ? 0 : static_cast<int>(inside->second.size());
                row.hasChildren = row.count > 0 || buildSubfolders.count(path) > 0;
                row.open = filtering || closedFolders.count(path) == 0;
                rows.push_back(row);

                const bool contentShown = row.open && row.hasChildren ? EmitFolderContents(path, depth + 1, filtering) : false;

                // While filtering, a folder is there for what it holds
                const bool nameMatches = !queryLower.empty() && Lower(row.label).find(queryLower) != std::string::npos;
                if (filtering && !contentShown && !nameMatches)
                    rows.resize(mark);
                else
                    shown = true;
            }
        }

        auto actors = buildActorsByFolder.find(folder);
        if (actors != buildActorsByFolder.end())
            for (const ActorPtr& actor : actors->second)
                if (EmitActor(actor, depth, filtering))
                    shown = true;

        return shown;
    }

    bool WorldTree::EmitActor(const ActorPtr& actor, int depth, bool filtering)
    {
        const bool matches = !filtering || MatchesFilter(actor);

        const size_t mark = rows.size();

        Row row;
        row.depth = depth;
        row.actor = actor;
        row.hasChildren = actor->GetChildrenCount() > 0;
        row.open = filtering || openActors.count(actor->GetID().GetAsInt()) > 0;
        rows.push_back(row);

        bool childShown = false;
        if (row.hasChildren && row.open)
        {
            std::vector<ActorPtr> children = ChildrenOf(actor);
            if (sortColumn >= 0)
                SortActors(children);

            for (const ActorPtr& child : children)
                if (EmitActor(child, depth + 1, filtering))
                    childShown = true;
        }

        // Only shown for what is under it
        if (filtering && !matches && !childShown)
        {
            rows.resize(mark);
            return false;
        }

        rows[mark].dimmed = filtering && !matches;
        return true;
    }

    int WorldTree::IndexOfActorRow(int actorId) const
    {
        for (size_t i = 0; i < rows.size(); ++i)
            if (!rows[i].isFolder && rows[i].actor->GetID().GetAsInt() == actorId)
                return static_cast<int>(i);
        return -1;
    }

    int WorldTree::IndexOfFolderRow(const std::string& path) const
    {
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].isFolder && rows[i].path == path)
                return static_cast<int>(i);
        return -1;
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Table
    // ---------------------------------------------------------------------------------------------------------------------

    void WorldTree::DrawTable(Engine::Worlds::World* world)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float footer = ImGui::GetTextLineHeightWithSpacing() + style.ItemSpacing.y;

        hoveredRow = -1;

        const ImVec2 tableMin = ImGui::GetCursorScreenPos();
        const ImVec2 tableSize(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y - footer, 60.0f));
        const ImVec2 tableMax(tableMin.x + tableSize.x, tableMin.y + tableSize.y);
        float headerHeight = 0.0f;

        const ImGuiTableFlags flags =
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
            ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_NoBordersInBody;

        if (ImGui::BeginTable("##Outliner", 5, flags, tableSize))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 1.0f, ColumnName);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f, ColumnType);
            ImGui::TableSetupColumn("Components", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide | ImGuiTableColumnFlags_NoSort, 180.0f, ColumnComponents);
            ImGui::TableSetupColumn("Children", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, 64.0f, ColumnChildren);
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, 56.0f, ColumnId);
            ImGui::TableHeadersRow();
            headerHeight = ImGui::TableGetHeaderRowHeight();

            // The sort applies to the next frame's rows
            if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs())
            {
                if (specs->SpecsCount > 0)
                {
                    sortColumn = static_cast<int>(specs->Specs[0].ColumnUserID);
                    sortAscending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                }
                else
                {
                    sortColumn = -1;
                }
                specs->SpecsDirty = false;
            }

            scrollRowIndex = -1;
            if (scrollToActorId >= 0)
                scrollRowIndex = IndexOfActorRow(scrollToActorId);
            else if (!scrollToFolder.empty())
                scrollRowIndex = IndexOfFolderRow(scrollToFolder);

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()));
            if (scrollRowIndex >= 0)
                clipper.IncludeItemByIndex(scrollRowIndex);

            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    DrawRow(i);

            scrollToActorId = -1;
            scrollToFolder.clear();

            ImGui::EndTable();
        }

        // (the scroll bar is not empty space)
        const bool tableHovered = ImGui::IsMouseHoveringRect(ImVec2(tableMin.x, tableMin.y + headerHeight),
                                                             ImVec2(tableMax.x - style.ScrollbarSize, tableMax.y)) &&
                                  ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_NoPopupHierarchy);

        // Empty space : a click deselects, a right-click opens the menu of the outliner, a drop lands at the top level
        if (tableHovered && hoveredRow < 0 && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        {
            ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyCtrl && !io.KeyShift)
            {
                parent->ClearSelection();
                selectedFolder.clear();
                selectionTouched = true;
            }

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
                ImGui::OpenPopup("##OutlinerEmptyContext");
        }

        if (ImGui::BeginPopup("##OutlinerEmptyContext"))
        {
            DrawEmptyContextMenu(world);
            ImGui::EndPopup();
        }

        HandleWindowDrop(world, ImVec2(tableMin.x, tableMin.y + headerHeight), tableMax);

        // Footer
        int actorRows = 0;
        for (const Row& row : rows)
            if (!row.isFolder)
                ++actorRows;
        ImGui::TextDisabled("%d actor%s", actorRows, actorRows == 1 ? "" : "s");
        if (parent->GetSelectionCount() > 0)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%d selected)", static_cast<int>(parent->GetSelectionCount()));
        }

        if (selectionTouched)
        {
            // The selection moved because of this panel : the next frame must not scroll to it as it would for a pick in the viewport
            auto primary = parent->GetSelectedActor();
            lastPrimaryId = primary ? primary->GetID().GetAsInt() : -1;
            selectionTouched = false;
        }
    }

    void WorldTree::DrawRow(int index)
    {
        const Row& row = rows[index];
        const ImGuiIO& io = ImGui::GetIO();

        const bool isFolder = row.isFolder;
        const int actorId = isFolder ? -1 : row.actor->GetID().GetAsInt();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        // Brought into view (selected from the viewport, found with the arrows...)
        if (index == scrollRowIndex)
            ImGui::SetScrollHereY(0.5f);

        if (isFolder)
            ImGui::PushID(row.path.c_str());
        else
            ImGui::PushID(actorId);

        const bool selected = isFolder ? (selectedFolder == row.path && !parent->HasSelection()) : parent->IsSelected(row.actor);
        const bool primary = !isFolder && row.actor == parent->GetSelectedActor() && parent->GetSelectionCount() > 1;
        const bool renaming = isFolder ? (renameKind == RenameKind::Folder && renameFolder == row.path)
                                       : (renameKind == RenameKind::Actor && renameActorId == actorId);

        const float lineHeight = ImGui::GetTextLineHeight();
        const ImVec2 cellPos = ImGui::GetCursorScreenPos();

        const bool clicked = ImGui::Selectable("##row", selected,
            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | (renaming ? ImGuiSelectableFlags_Disabled : 0),
            ImVec2(0.0f, 0.0f));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        const ImVec2 rowMax = ImGui::GetItemRectMax();

        if (hovered)
            hoveredRow = index;

        // A right-click on a row outside of the selection selects it : the menu is about the selection
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected)
            ActivateRow(index, false, false);

        if (ImGui::BeginPopupContextItem("##RowContext"))
        {
            if (isFolder)
                DrawFolderContextMenu(row.path);
            else
                DrawActorContextMenu(row.actor);
            ImGui::EndPopup();
        }

        // Drag : the selected actors (a row that is not selected is selected by the drag), or the folder
        if (ImGui::BeginDragDropSource())
        {
            if (isFolder)
            {
                ImGui::SetDragDropPayload(kFolderPayload, row.path.c_str(), row.path.size() + 1);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kFolderColor), "%s", ICON_LC_FOLDER);
                ImGui::SameLine();
                ImGui::TextUnformatted(row.label.c_str());
            }
            else
            {
                if (!parent->IsSelected(row.actor))
                {
                    parent->SetSelectedActor(row.actor);
                    selectedFolder.clear();
                    selectionTouched = true;
                }

                std::vector<int> ids;
                for (const auto& actor : SelectedTopLevel())
                    ids.push_back(actor->GetID().GetAsInt());
                ImGui::SetDragDropPayload(kActorsPayload, ids.data(), ids.size() * sizeof(int));

                if (ids.size() == 1)
                    ImGui::TextUnformatted(row.actor->GetName().c_str());
                else
                    ImGui::Text("%d actors", static_cast<int>(ids.size()));
            }
            ImGui::EndDragDropSource();
        }

        // Drop : actors go under an actor / into a folder, a folder goes into a folder, assets are placed in a folder
        if (ImGui::BeginDragDropTarget())
        {
            auto droppedActors = [](const ImGuiPayload* payload){
                std::vector<ActorPtr> actors;
                const int* ids = static_cast<const int*>(payload->Data);
                for (size_t i = 0; i < payload->DataSize / sizeof(int); ++i)
                    if (auto actor = ActorFromId(ids[i]))
                        actors.push_back(actor);
                return actors;
            };

            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kActorsPayload))
            {
                std::vector<ActorPtr> actors = droppedActors(payload);
                if (isFolder)
                {
                    const std::string folder = row.path;
                    pendingEdit = [this, actors, folder]{
                        Commands::Actions::MoveToFolder(actors, folder);
                        closedFolders.erase(folder);
                    };
                }
                else
                {
                    ActorPtr target = row.actor;
                    pendingEdit = [this, actors, target]{ ReparentSelection(actors, target); };
                }
            }

            if (isFolder)
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kFolderPayload))
                {
                    const std::string source = static_cast<const char*>(payload->Data);
                    const std::string destination = row.path;
                    pendingEdit = [this, source, destination]{
                        const std::string moved = destination + "/" + LeafOf(source);
                        if (Commands::Actions::RenameFolder(source, moved))
                            closedFolders.erase(destination);
                        else
                            Notifications::Warning("Cannot move the folder \"%s\" there", LeafOf(source).c_str());
                    };
                }

                std::vector<std::string> assets = DragDrop::AcceptAssetDragDropPayload();
                if (!assets.empty())
                {
                    const std::string folder = row.path;
                    auto* world = GetEngine().GetWorldManager()->GetWorldAt(0);
                    pendingEdit = [this, world, assets, folder]{ SpawnAssets(world, assets, folder); };
                }
            }

            ImGui::EndDragDropTarget();
        }

        if (clicked && !renaming)
            ActivateRow(index, io.KeyCtrl, io.KeyShift);

        if (hovered && !renaming && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (isFolder)
            {
                if (closedFolders.count(row.path))
                    closedFolders.erase(row.path);
                else
                    closedFolders.insert(row.path);
            }
            else if (parent->viewport)
            {
                parent->viewport->FocusSelected();
            }
        }

        // The primary actor of a multiple selection has a mark
        if (primary)
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(rowMin.x, rowMin.y), ImVec2(rowMin.x + 3.0f, rowMax.y),
                                                      ImGui::GetColorU32(ImGuiCol_PlotHistogram));

        // Open / close arrow
        const ImVec2 arrowPos(cellPos.x + row.depth * kIndent, cellPos.y);
        if (row.hasChildren)
        {
            ImGui::SetCursorScreenPos(arrowPos);
            const bool toggled = ImGui::InvisibleButton("##arrow", ImVec2(lineHeight, lineHeight));
            const bool arrowHovered = ImGui::IsItemHovered();

            ImGui::RenderArrow(ImGui::GetWindowDrawList(), ImVec2(arrowPos.x + 1.0f, arrowPos.y),
                               ImGui::GetColorU32(arrowHovered ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                               row.open ? ImGuiDir_Down : ImGuiDir_Right, 1.0f);

            // While filtering, everything is open
            if (toggled && !HasActiveFilter())
            {
                if (isFolder)
                {
                    if (row.open)
                        closedFolders.insert(row.path);
                    else
                        closedFolders.erase(row.path);
                }
                else
                {
                    if (row.open)
                        openActors.erase(actorId);
                    else
                        openActors.insert(actorId);
                }
            }
        }

        // Icon and name
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float x = arrowPos.x + lineHeight + 2.0f;

        IconInfo icon;
        if (isFolder)
            icon = { row.open && row.hasChildren ? ICON_LC_FOLDER_OPEN : ICON_LC_FOLDER, kFolderColor };
        else
            icon = IconOf(row.actor);

        const ImU32 textColor = ImGui::GetColorU32(row.dimmed ? ImGuiCol_TextDisabled : ImGuiCol_Text);
        if (row.dimmed)
            icon.color = (icon.color & 0x00FFFFFFu) | 0x70000000u;

        drawList->AddText(ImVec2(x, cellPos.y), icon.color, icon.icon);
        x += ImGui::CalcTextSize(icon.icon).x + 6.0f;

        if (renaming)
        {
            ImGui::SetCursorScreenPos(ImVec2(x, cellPos.y));
            DrawRenameBox(ImGui::GetContentRegionAvail().x);
        }
        else if (isFolder)
        {
            drawList->AddText(ImVec2(x, cellPos.y), textColor, row.label.c_str());
            if (row.count > 0)
            {
                char count[32];
                snprintf(count, sizeof(count), "(%d)", row.count);
                drawList->AddText(ImVec2(x + ImGui::CalcTextSize(row.label.c_str()).x + 6.0f, cellPos.y),
                                  ImGui::GetColorU32(ImGuiCol_TextDisabled), count);
            }
        }
        else
        {
            drawList->AddText(ImVec2(x, cellPos.y), textColor, row.actor->GetName().c_str());
        }

        // The other columns
        if (ImGui::TableSetColumnIndex(1))
        {
            if (isFolder)
                ImGui::TextDisabled("Folder");
            else
                ImGui::TextUnformatted(TypeOf(row.actor).c_str());
        }

        if (ImGui::TableSetColumnIndex(2) && !isFolder)
        {
            std::string components;
            for (const std::string& name : ComponentNames(row.actor))
                components += (components.empty() ? "" : ", ") + name;
            ImGui::TextUnformatted(components.c_str());
        }

        if (ImGui::TableSetColumnIndex(3))
        {
            if (isFolder)
                ImGui::TextDisabled("%d", row.count);
            else
                ImGui::TextDisabled("%d", row.actor->GetChildrenCount());
        }

        if (ImGui::TableSetColumnIndex(4) && !isFolder)
            ImGui::TextDisabled("%d", actorId);

        ImGui::PopID();
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Selection
    // ---------------------------------------------------------------------------------------------------------------------

    std::vector<WorldTree::ActorPtr> WorldTree::SelectedTopLevel() const
    {
        return Commands::Actions::TopLevel(parent->GetSelectedActors());
    }

    void WorldTree::ActivateRow(int index, bool ctrl, bool shift)
    {
        if (index < 0 || index >= static_cast<int>(rows.size()))
            return;

        const Row& row = rows[index];
        selectionTouched = true;

        if (row.isFolder)
        {
            // A folder is selected alone
            parent->ClearSelection();
            selectedFolder = row.path;
            return;
        }

        selectedFolder.clear();
        const int id = row.actor->GetID().GetAsInt();

        if (shift)
        {
            SelectRange(index, ctrl);
        }
        else if (ctrl)
        {
            parent->ToggleSelection(row.actor);
            anchorId = id;
        }
        else
        {
            parent->SetSelectedActor(row.actor);
            anchorId = id;
        }
    }

    void WorldTree::SelectRange(int index, bool additive)
    {
        int anchor = anchorId >= 0 ? IndexOfActorRow(anchorId) : -1;
        if (anchor < 0)
        {
            anchor = index;
            anchorId = rows[index].actor->GetID().GetAsInt();
        }

        const int low = std::min(anchor, index);
        const int high = std::max(anchor, index);

        std::vector<ActorPtr> selection = additive ? parent->GetSelectedActors() : std::vector<ActorPtr>();

        // Walking from the anchor to the row, so that the row is the last one : the primary actor
        std::vector<ActorPtr> range;
        for (int i = low; i <= high; ++i)
            if (!rows[i].isFolder)
                range.push_back(rows[i].actor);
        if (anchor > index)
            std::reverse(range.begin(), range.end());

        selection.insert(selection.end(), range.begin(), range.end());
        parent->SetSelectedActors(std::move(selection));
    }

    void WorldTree::MoveCursor(int delta, bool extend)
    {
        if (rows.empty())
            return;

        int current = -1;
        if (auto primary = parent->GetSelectedActor())
            current = IndexOfActorRow(primary->GetID().GetAsInt());
        else if (!selectedFolder.empty())
            current = IndexOfFolderRow(selectedFolder);

        const int next = current < 0 ? (delta > 0 ? 0 : static_cast<int>(rows.size()) - 1)
                                     : std::clamp(current + delta, 0, static_cast<int>(rows.size()) - 1);
        if (next == current)
            return;

        ActivateRow(next, false, extend);

        if (rows[next].isFolder)
            scrollToFolder = rows[next].path;
        else
            scrollToActorId = rows[next].actor->GetID().GetAsInt();
    }

    void WorldTree::RevealActor(const ActorPtr& actor)
    {
        ActorPtr root = actor;
        for (ActorPtr p = ParentActorOf(actor); p; p = ParentActorOf(p))
        {
            openActors.insert(p->GetID().GetAsInt());
            root = p;
        }

        // Its folders
        for (std::string folder = root->GetFolder(); !folder.empty(); folder = ParentPathOf(folder))
            closedFolders.erase(folder);

        scrollToActorId = actor->GetID().GetAsInt();
    }

    void WorldTree::UpdateReveal()
    {
        auto primary = parent->GetSelectedActor();
        const int id = primary ? primary->GetID().GetAsInt() : -1;
        if (id == lastPrimaryId)
            return;

        // Selected from somewhere else (the viewport, an undo) : the row is shown
        lastPrimaryId = id;
        if (primary)
        {
            selectedFolder.clear();
            RevealActor(primary);
        }
    }

    void WorldTree::ExpandAll(Engine::Worlds::World* world)
    {
        for (const auto& [id, actor] : world->GetRootActors())
            if (actor)
                SetSubtreeOpen(actor, true, openActors);
        closedFolders.clear();
    }

    void WorldTree::CollapseAll(Engine::Worlds::World* world)
    {
        openActors.clear();
        closedFolders = CollectFolders(world);
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Shortcuts
    // ---------------------------------------------------------------------------------------------------------------------

    void WorldTree::HandleShortcuts()
    {
        ImGuiIO& io = ImGui::GetIO();

        // The panel has the focus, and no text box is using the keys (the search, a rename)
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || io.WantTextInput || renameKind != RenameKind::None)
            return;

        auto pressed = [&](ImGuiKey key, bool ctrl = false, bool shift = false, bool repeat = false){
            return io.KeyCtrl == ctrl && io.KeyShift == shift && !io.KeyAlt && ImGui::IsKeyPressed(key, repeat);
        };

        const bool hasActors = parent->HasSelection();
        const bool hasFolder = !selectedFolder.empty();

        if (pressed(ImGuiKey_F2))
        {
            if (hasActors)
                BeginRename(parent->GetSelectedActor());
            else if (hasFolder)
                BeginRenameFolder(selectedFolder);
        }
        else if (pressed(ImGuiKey_Delete))
        {
            if (hasActors)
                pendingEdit = [this]{ parent->DeleteSelection(); };
            else if (hasFolder)
                DeleteFolder(selectedFolder);
        }
        else if (pressed(ImGuiKey_C, true) && hasActors)
        {
            parent->CopySelection();
        }
        else if (pressed(ImGuiKey_X, true) && hasActors)
        {
            pendingEdit = [this]{ parent->CutSelection(); };
        }
        else if (pressed(ImGuiKey_V, true) && parent->CanPaste())
        {
            const std::string folder = hasActors ? std::string() : selectedFolder;
            pendingEdit = [this, folder]{
                auto& stack = Commands::CommandStack::Get();
                stack.Begin("Paste Actors");
                parent->PasteClipboard();
                if (!folder.empty())
                    Commands::Actions::MoveToFolder(parent->GetSelectedActors(), folder);
                stack.End();
            };
        }
        else if (pressed(ImGuiKey_D, true) && hasActors)
        {
            pendingEdit = [this]{ parent->DuplicateSelection(); };
        }
        else if (pressed(ImGuiKey_A, true))
        {
            parent->SelectAll();
            selectedFolder.clear();
            selectionTouched = true;
        }
        else if (pressed(ImGuiKey_G, true) && hasActors)
        {
            pendingEdit = [this]{
                parent->GroupSelection();
                if (auto group = parent->GetSelectedActor())
                    BeginRename(group);
            };
        }
        else if (pressed(ImGuiKey_N, true, true))
        {
            NewFolder(hasActors);
        }
        else if (pressed(ImGuiKey_F, true))
        {
            focusSearch = true;
        }
        else if (pressed(ImGuiKey_Escape) && (hasActors || hasFolder))
        {
            parent->ClearSelection();
            selectedFolder.clear();
            selectionTouched = true;
        }
        else if (pressed(ImGuiKey_DownArrow, false, false, true) || pressed(ImGuiKey_DownArrow, false, true, true))
        {
            MoveCursor(1, io.KeyShift);
        }
        else if (pressed(ImGuiKey_UpArrow, false, false, true) || pressed(ImGuiKey_UpArrow, false, true, true))
        {
            MoveCursor(-1, io.KeyShift);
        }
        else if (pressed(ImGuiKey_Home))
        {
            MoveCursor(-static_cast<int>(rows.size()), false);
        }
        else if (pressed(ImGuiKey_End))
        {
            MoveCursor(static_cast<int>(rows.size()), false);
        }
        else if (pressed(ImGuiKey_LeftArrow, false, false, true) || pressed(ImGuiKey_RightArrow, false, false, true))
        {
            int current = -1;
            if (auto primary = parent->GetSelectedActor())
                current = IndexOfActorRow(primary->GetID().GetAsInt());
            else if (hasFolder)
                current = IndexOfFolderRow(selectedFolder);
            if (current < 0)
                return;

            const Row& row = rows[current];
            const bool filtering = HasActiveFilter();
            const bool left = ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true);

            auto setOpen = [&](bool open){
                if (row.isFolder)
                {
                    if (open)
                        closedFolders.erase(row.path);
                    else
                        closedFolders.insert(row.path);
                }
                else
                {
                    if (open)
                        openActors.insert(row.actor->GetID().GetAsInt());
                    else
                        openActors.erase(row.actor->GetID().GetAsInt());
                }
            };

            if (left)
            {
                if (row.hasChildren && row.open && !filtering)
                {
                    setOpen(false);
                }
                else
                {
                    // To the line it hangs from
                    for (int i = current - 1; i >= 0; --i)
                        if (rows[i].depth < row.depth)
                        {
                            ActivateRow(i, false, false);
                            if (rows[i].isFolder)
                                scrollToFolder = rows[i].path;
                            else
                                scrollToActorId = rows[i].actor->GetID().GetAsInt();
                            break;
                        }
                }
            }
            else
            {
                if (row.hasChildren && !row.open)
                    setOpen(true);
                else if (current + 1 < static_cast<int>(rows.size()) && rows[current + 1].depth > row.depth)
                    MoveCursor(1, false);
            }
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Menus
    // ---------------------------------------------------------------------------------------------------------------------

    void WorldTree::DrawMoveToFolderMenu()
    {
        if (!ImGui::BeginMenu(ICON_LC_FOLDER_INPUT "  Move To Folder"))
            return;

        auto* world = GetEngine().GetWorldManager()->GetWorldAt(0);

        if (ImGui::MenuItem(ICON_LC_FOLDER_PLUS "  New Folder"))
            NewFolder(true);

        if (ImGui::MenuItem("Top Level"))
            pendingEdit = [this]{ Commands::Actions::MoveToFolder(parent->GetSelectedActors(), std::string()); };

        if (world)
        {
            ImGui::Separator();
            for (const std::string& path : CollectFolders(world))
            {
                if (ImGui::MenuItem(path.c_str()))
                {
                    pendingEdit = [this, path]{
                        Commands::Actions::MoveToFolder(parent->GetSelectedActors(), path);
                        closedFolders.erase(path);
                    };
                }
            }
        }

        ImGui::EndMenu();
    }

    void WorldTree::DrawActorContextMenu(const ActorPtr& actor)
    {
        const size_t count = parent->GetSelectionCount();
        const bool single = count == 1;

        if (count > 1)
            ImGui::TextDisabled("%d actors", static_cast<int>(count));

        if (ImGui::MenuItem(ICON_LC_PENCIL "  Rename", "F2", false, single))
            BeginRename(actor);

        ImGui::Separator();

        if (ImGui::MenuItem(ICON_LC_COPY "  Copy", "Ctrl+C"))
            parent->CopySelection();
        if (ImGui::MenuItem(ICON_LC_SCISSORS "  Cut", "Ctrl+X"))
            pendingEdit = [this]{ parent->CutSelection(); };
        if (ImGui::MenuItem(ICON_LC_CLIPBOARD_PASTE "  Paste", "Ctrl+V", false, parent->CanPaste()))
            pendingEdit = [this]{ parent->PasteClipboard(); };
        if (ImGui::MenuItem(ICON_LC_COPY_PLUS "  Duplicate", "Ctrl+D"))
            pendingEdit = [this]{ parent->DuplicateSelection(); };
        if (ImGui::MenuItem(ICON_LC_TRASH_2 "  Delete", "Del"))
            pendingEdit = [this]{ parent->DeleteSelection(); };

        ImGui::Separator();

        if (single && ImGui::BeginMenu(ICON_LC_PLUS "  Create Child"))
        {
            DrawCreateMenu(actor, std::string());
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem(ICON_LC_GROUP "  Group", "Ctrl+G"))
        {
            pendingEdit = [this]{
                parent->GroupSelection();
                if (auto group = parent->GetSelectedActor())
                    BeginRename(group);
            };
        }

        DrawMoveToFolderMenu();

        bool anyParented = false;
        for (const auto& selected : parent->GetSelectedActors())
            anyParented = anyParented || ParentActorOf(selected) != nullptr;
        if (ImGui::MenuItem(ICON_LC_UNGROUP "  Detach From Parent", nullptr, false, anyParented))
            pendingEdit = [this]{ ReparentSelection(parent->GetSelectedActors(), nullptr); };

        ImGui::Separator();

        if (ImGui::BeginMenu(ICON_LC_MOUSE_POINTER_CLICK "  Select"))
        {
            if (ImGui::MenuItem("Children"))
                pendingEdit = [this]{ parent->SelectChildren(); };
            if (ImGui::MenuItem("Parent", nullptr, false, single && ParentActorOf(actor) != nullptr))
                pendingEdit = [this, actor]{ parent->SetSelectedActor(ParentActorOf(actor)); };
            if (ImGui::MenuItem("Invert Selection"))
                pendingEdit = [this]{ parent->InvertSelection(); };
            if (ImGui::MenuItem("Select All", "Ctrl+A"))
                pendingEdit = [this]{ parent->SelectAll(); };
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(ICON_LC_CHEVRONS_UP_DOWN "  Hierarchy"))
        {
            if (ImGui::MenuItem("Expand Children"))
                for (const auto& selected : parent->GetSelectedActors())
                    SetSubtreeOpen(selected, true, openActors);
            if (ImGui::MenuItem("Collapse Children"))
                for (const auto& selected : parent->GetSelectedActors())
                    SetSubtreeOpen(selected, false, openActors);
            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::MenuItem(ICON_LC_FOCUS "  Focus In Viewport", "F", false, parent->viewport != nullptr))
            parent->viewport->FocusSelected();

        if (ImGui::MenuItem("Copy Name"))
        {
            std::string names;
            for (const auto& selected : parent->GetSelectedActors())
                names += (names.empty() ? "" : "\n") + selected->GetName();
            ImGui::SetClipboardText(names.c_str());
        }
    }

    void WorldTree::DrawFolderContextMenu(const std::string& path)
    {
        if (ImGui::MenuItem(ICON_LC_PENCIL "  Rename", "F2"))
            BeginRenameFolder(path);

        if (ImGui::MenuItem(ICON_LC_FOLDER_PLUS "  New Subfolder"))
        {
            pendingEdit = [this, path]{
                const std::string created = Commands::Actions::CreateFolder(path + "/New Folder");
                if (!created.empty())
                {
                    closedFolders.erase(path);
                    BeginRenameFolder(created);
                }
            };
        }

        if (ImGui::BeginMenu(ICON_LC_PLUS "  Create Actor Here"))
        {
            DrawCreateMenu(nullptr, path);
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem(ICON_LC_CLIPBOARD_PASTE "  Paste Here", "Ctrl+V", false, parent->CanPaste()))
        {
            pendingEdit = [this, path]{
                auto& stack = Commands::CommandStack::Get();
                stack.Begin("Paste Actors");
                parent->PasteClipboard();
                Commands::Actions::MoveToFolder(parent->GetSelectedActors(), path);
                stack.End();
                closedFolders.erase(path);
            };
        }

        ImGui::Separator();

        if (ImGui::MenuItem(ICON_LC_MOUSE_POINTER_CLICK "  Select Actors In Folder"))
        {
            pendingEdit = [this, path]{
                std::vector<ActorPtr> inside;
                if (auto* world = GetEngine().GetWorldManager()->GetWorldAt(0))
                    for (const auto& [id, actor] : world->GetRootActors())
                        if (actor && Commands::Actions::IsInFolder(Commands::Actions::NormalizeFolderPath(actor->GetFolder()), path))
                            inside.push_back(actor);
                parent->SetSelectedActors(std::move(inside));
                selectedFolder.clear();
                selectionTouched = true;
            };
        }

        ImGui::Separator();

        if (ImGui::MenuItem(ICON_LC_TRASH_2 "  Delete Folder"))
            DeleteFolder(path);
        ImGui::SetItemTooltip("The actors inside move up to the folder around it");

        if (ImGui::MenuItem(ICON_LC_TRASH_2 "  Delete Folder And Actors"))
            pendingEdit = [path]{ Commands::Actions::DeleteFolder(path, true); };
    }

    void WorldTree::DrawEmptyContextMenu(Engine::Worlds::World* world)
    {
        if (ImGui::BeginMenu(ICON_LC_PLUS "  Create"))
        {
            DrawCreateMenu(nullptr, std::string());
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem(ICON_LC_FOLDER_PLUS "  New Folder", "Ctrl+Shift+N"))
            NewFolder(false);

        ImGui::Separator();

        if (ImGui::MenuItem(ICON_LC_CLIPBOARD_PASTE "  Paste", "Ctrl+V", false, parent->CanPaste()))
            pendingEdit = [this]{ parent->PasteClipboard(); };

        if (ImGui::MenuItem("Select All", "Ctrl+A"))
            pendingEdit = [this]{ parent->SelectAll(); selectedFolder.clear(); };

        ImGui::Separator();

        if (ImGui::MenuItem(ICON_LC_UNFOLD_VERTICAL "  Expand All"))
            ExpandAll(world);
        if (ImGui::MenuItem(ICON_LC_FOLD_VERTICAL "  Collapse All"))
            CollapseAll(world);
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Drop on empty space
    // ---------------------------------------------------------------------------------------------------------------------

    void WorldTree::HandleWindowDrop(Engine::Worlds::World* world, const ImVec2& min, const ImVec2& max)
    {
        // A row under the cursor takes the drop itself
        if (hoveredRow >= 0)
            return;

        if (!ImGui::BeginDragDropTargetCustom(ImRect(min, max), ImGui::GetID("##OutlinerDropZone")))
            return;

        // Assets (meshes) become new actors at the top level
        std::vector<std::string> assets = DragDrop::AcceptAssetDragDropPayload();
        if (!assets.empty())
            SpawnAssets(world, assets, std::string());

        // Actors are detached : root actors, at the top of the outliner
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kActorsPayload))
        {
            std::vector<ActorPtr> actors;
            const int* ids = static_cast<const int*>(payload->Data);
            for (size_t i = 0; i < payload->DataSize / sizeof(int); ++i)
                if (auto actor = ActorFromId(ids[i]))
                    actors.push_back(actor);
            pendingEdit = [actors]{ Commands::Actions::MoveToFolder(actors, std::string()); };
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kFolderPayload))
        {
            const std::string source = static_cast<const char*>(payload->Data);
            pendingEdit = [source]{
                if (!Commands::Actions::RenameFolder(source, LeafOf(source)) && source.find('/') != std::string::npos)
                    Notifications::Warning("Cannot move the folder \"%s\" to the top: that name is taken", LeafOf(source).c_str());
            };
        }

        ImGui::EndDragDropTarget();
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Edits
    // ---------------------------------------------------------------------------------------------------------------------

    void WorldTree::ReparentSelection(const std::vector<ActorPtr>& actors, const ActorPtr& newParent)
    {
        const int moved = Commands::Actions::ReparentMany(actors, newParent);
        if (moved > 0 && newParent)
            openActors.insert(newParent->GetID().GetAsInt());
    }

    void WorldTree::SpawnAssets(Engine::Worlds::World* world, const std::vector<std::string>& assets, const std::string& folder)
    {
        if (!world)
            return;

        std::vector<ActorPtr> placed;

        auto& history = Commands::CommandStack::Get();
        history.Begin("Place Actors");

        for (const auto& nameInProject : assets)
        {
            // Only static meshes can be dropped directly into the scene this way - other asset types
            // (materials, textures, ...) are assigned onto existing fields instead
            if (Engine::Filesystem::Path(nameInProject).GetExtensionType() != Engine::Filesystem::Type::T_MODEL)
                continue;

            auto actor = Engine::Core::Object::CreateWithContext<Engine::Objects::Actor>(
                &GetEngine(), Engine::Filesystem::Path(nameInProject).GetFilename(false), &GetEngine());
            world->AddActor(actor);
            actor->SetFolder(folder);

            auto model = actor->AddComponent<Engine::Objects::Components::Model>();
            if (model)
                model->SetMesh(nameInProject);

            Commands::Actions::RecordCreated(actor, "Place Actor");
            placed.push_back(actor);
        }

        history.End();

        if (!placed.empty())
        {
            selectedFolder.clear();
            parent->SetSelectedActors(placed);
            selectionTouched = true;
        }
    }

    void WorldTree::NewFolder(bool withSelection)
    {
        pendingEdit = [this, withSelection]{
            auto& stack = Commands::CommandStack::Get();
            stack.Begin("New Folder");

            // Inside the selected folder, when it is not the actors that are going in
            std::string wanted = "New Folder";
            if (!withSelection && !selectedFolder.empty())
                wanted = selectedFolder + "/" + wanted;

            const std::string path = Commands::Actions::CreateFolder(wanted);
            if (!path.empty() && withSelection)
                Commands::Actions::MoveToFolder(parent->GetSelectedActors(), path);

            stack.End();

            if (!path.empty())
            {
                for (std::string open = path; !open.empty(); open = ParentPathOf(open))
                    closedFolders.erase(open);
                BeginRenameFolder(path);
            }
        };
    }

    void WorldTree::DeleteFolder(const std::string& path)
    {
        pendingEdit = [this, path]{
            Commands::Actions::DeleteFolder(path, false);
            if (selectedFolder == path)
                selectedFolder.clear();
        };
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Rename
    // ---------------------------------------------------------------------------------------------------------------------

    void WorldTree::BeginRename(const std::shared_ptr<Engine::Objects::Actor>& actor)
    {
        if (!actor)
            return;

        renameKind = RenameKind::Actor;
        renameActorId = actor->GetID().GetAsInt();
        snprintf(renameBuffer, sizeof(renameBuffer), "%s", actor->GetName().c_str());
        renameNeedsFocus = true;
        renameStartFrame = ImGui::GetFrameCount();

        RevealActor(actor);
    }

    void WorldTree::BeginRenameFolder(const std::string& path)
    {
        if (path.empty())
            return;

        renameKind = RenameKind::Folder;
        renameFolder = path;
        snprintf(renameBuffer, sizeof(renameBuffer), "%s", LeafOf(path).c_str());
        renameNeedsFocus = true;
        renameStartFrame = ImGui::GetFrameCount();

        parent->ClearSelection();
        selectedFolder = path;
        scrollToFolder = path;
        for (std::string open = ParentPathOf(path); !open.empty(); open = ParentPathOf(open))
            closedFolders.erase(open);
    }

    void WorldTree::DrawRenameBox(float width)
    {
        ImGui::SetNextItemWidth(std::max(width - 4.0f, 40.0f));
        if (renameNeedsFocus)
        {
            ImGui::SetKeyboardFocusHere();
            renameNeedsFocus = false;
        }

        const bool entered = ImGui::InputText("##rename", renameBuffer, sizeof(renameBuffer),
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const bool deactivated = ImGui::IsItemDeactivated();

        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            CancelRename();
        // Clicking elsewhere ends it like Enter does (not on the frames the box is still taking the focus)
        else if (entered || (deactivated && ImGui::GetFrameCount() > renameStartFrame + 1))
            CommitRename();
    }

    void WorldTree::CommitRename()
    {
        const RenameKind kind = renameKind;
        const int actorId = renameActorId;
        const std::string folder = renameFolder;
        const std::string name = renameBuffer;
        CancelRename();

        if (name.empty())
            return;

        pendingEdit = [this, kind, actorId, folder, name]{
            if (kind == RenameKind::Actor)
            {
                if (auto actor = ActorFromId(actorId))
                    Commands::Actions::Rename(actor, name);
                return;
            }

            const std::string leaf = Commands::Actions::NormalizeFolderPath(name);
            if (leaf.empty() || leaf == LeafOf(folder))
                return;

            const std::string path = ParentPathOf(folder).empty() ? leaf : ParentPathOf(folder) + "/" + leaf;
            if (Commands::Actions::RenameFolder(folder, path))
            {
                selectedFolder = path;
                // Closed stays closed
                if (closedFolders.erase(folder))
                    closedFolders.insert(path);
            }
            else
            {
                Notifications::Warning("Cannot rename the folder to \"%s\": that name is taken", leaf.c_str());
            }
        };
    }
}
