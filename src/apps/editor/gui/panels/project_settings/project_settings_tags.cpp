// The "Tags" category of the Project Settings : the tree of the tags, the project's own edited in place (see docs/concepts/tags.md)

#include "project_settings_panel.hpp"

#include "engine/assets/project/project.hpp"
#include "engine/core/tags/tag_registry.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/world/world_tags.hpp"

#include "apps/editor/gui/IconsLucide.h"
#include "apps/editor/gui/notifications.hpp"

#include "imgui/imgui.h"

#include <cstdio>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using namespace Shard::Engine;
using Shard::Engine::Core::GetEngine;

namespace Shard::Editor::GUI{

    namespace {

        const ImVec4 kSourceColor(0.55f, 0.65f, 0.85f, 1.0f);
        const ImVec4 kErrorColor(0.90f, 0.35f, 0.35f, 1.0f);

        bool IsSameOrUnder(const std::string& name, const std::string& root)
        {
            return name == root || (name.size() > root.size() && name.compare(0, root.size(), root) == 0 && name[root.size()] == '.');
        }

        /// The tag and everything under it is the project's own : the editor may rename or delete it
        bool OwnedByProject(Core::Tag tag)
        {
            std::vector<Core::Tag> tree;
            Core::TagRegistry::Get().CollectDescendants(tag, tree);
            for (Core::Tag t : tree)
                if (const Core::TagInfo* info = Core::TagRegistry::Get().GetInfo(t))
                    for (const Core::TagDeclaration& d : info->declarations)
                        if (d.source != Core::TagSource::Project)
                            return false;
            return true;
        }

        void Tooltip(const char* text)
        {
            if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                return;
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        Worlds::World* OpenWorld()
        {
            return GetEngine().GetWorldManager() ? GetEngine().GetWorldManager()->GetWorldAt(0) : nullptr;
        }
    }

    void ProjectSettingsPanel::ApplyTags()
    {
        Projects::ApplyTagSettings(*GetEngine().GetCurrentProject()->GetTagSettings());
        m_LastDirtyCheck = -1.0;
    }

    bool ProjectSettingsPanel::AddTag(const std::string& name, std::string* error)
    {
        Projects::TagSettings* settings = GetEngine().GetCurrentProject()->GetTagSettings();
        if (!Core::TagRegistry::IsValidName(name, error))
            return false;
        if (Core::TagRegistry::Get().Find(name).IsValid() && settings->tags.count(name)) {
            if (error)
                *error = "'" + name + "' exists already";
            return false;
        }
        settings->tags.emplace(name, std::string());
        // A redirect away from a name that is declared again would hide it
        settings->redirects.erase(name);
        ApplyTags();
        return true;
    }

    void ProjectSettingsPanel::DeleteTag(const std::string& name)
    {
        Projects::TagSettings* settings = GetEngine().GetCurrentProject()->GetTagSettings();
        for (auto it = settings->tags.begin(); it != settings->tags.end();)
            it = IsSameOrUnder(it->first, name) ? settings->tags.erase(it) : std::next(it);
        ApplyTags();
        Notifications::Info("Deleted the tag %s : what still carries it keeps it, as a tag nobody declares", name.c_str());
    }

    bool ProjectSettingsPanel::RenameTag(const std::string& from, const std::string& to, std::string* error)
    {
        Projects::TagSettings* settings = GetEngine().GetCurrentProject()->GetTagSettings();
        Core::TagRegistry& registry = Core::TagRegistry::Get();

        if (!Core::TagRegistry::IsValidName(to, error))
            return false;
        if (to == from)
            return true;
        if (registry.Find(to).IsValid() && registry.ApplyRedirects(to) == to) {
            if (error)
                *error = "'" + to + "' exists already";
            return false;
        }
        if (IsSameOrUnder(to, from)) {
            if (error)
                *error = "a tag cannot be moved under itself";
            return false;
        }

        // Every name of the subtree, the ones only there because of a child included : each one gets its redirect
        std::vector<Core::Tag> tree;
        registry.CollectDescendants(registry.Find(from), tree);
        std::vector<std::pair<std::string, std::string>> renamed;
        for (Core::Tag t : tree) {
            const std::string name = t.ToString();
            renamed.emplace_back(name, to + name.substr(from.size()));
        }

        std::map<std::string, std::string> tags;
        for (auto& [name, comment] : settings->tags)
            tags[IsSameOrUnder(name, from) ? to + name.substr(from.size()) : name] = comment;
        settings->tags = std::move(tags);

        // The old redirects that led to a renamed tag now lead to its new name, then the renamed ones are added
        for (auto& [old, target] : settings->redirects)
            if (IsSameOrUnder(target, from))
                target = to + target.substr(from.size());
        for (auto& [old, now] : renamed) {
            settings->redirects[old] = now;
            settings->redirects.erase(now);
        }
        ApplyTags();

        // The open world follows at once ; the others when they are loaded, through the redirects
        size_t changed = 0;
        if (Worlds::World* world = OpenWorld()) {
            changed = Worlds::RemapTags(*world, [&](Core::Tag tag) {
                const std::string name = tag.ToString();
                if (!IsSameOrUnder(name, from))
                    return Core::Tag();
                return registry.Resolve(to + name.substr(from.size()));
            });
            if (changed)
                world->SetDirty(true);
        }

        Notifications::Info("Renamed %s to %s (%zu object(s) of the open world changed ; the other worlds follow when they are loaded)", from.c_str(), to.c_str(), changed);
        return true;
    }

    void ProjectSettingsPanel::FindTagReferences(Core::Tag tag)
    {
        m_TagReferences.clear();
        m_TagReferencesOf = tag.ToString();

        // The open world as it is now (what is not saved yet included), the others as their files are
        Worlds::World* world = OpenWorld();
        std::string openWorld;
        if (world) {
            openWorld = GetEngine().GetFileManager()->GetFileInfos(world->GetPath()).nameInProject;
            std::vector<std::string> found;
            Worlds::CollectTagReferences(*world, tag, found);
            for (const std::string& where : found)
                m_TagReferences.push_back(openWorld + " : " + where);
        }

        for (const auto& [id, info] : GetEngine().GetAssetIDManager()->AssetIDMap) {
            if (!info || info->baseInfos.type != Filesystem::Type::T_WORLD || info->baseInfos.nameInProject == openWorld)
                continue;
            const nlohmann::json data = nlohmann::json::parse(info->baseInfos.path.ReadFile(), nullptr, false);
            if (data.is_discarded())
                continue;
            std::vector<std::string> found;
            Worlds::CollectTagReferences(data, m_TagReferencesOf, found);
            for (const std::string& where : found)
                m_TagReferences.push_back(info->baseInfos.nameInProject + " : " + where);
        }
    }

    void ProjectSettingsPanel::DrawTagNode(Core::Tag tag)
    {
        Core::TagRegistry& registry = Core::TagRegistry::Get();
        const Core::TagInfo* info = registry.GetInfo(tag);
        if (!info)
            return;

        // Copied : editing the project's tags rebuilds the tree, `info` does not survive it
        const std::string name = info->name;
        const std::vector<Core::Tag> children = info->children;
        const uint8_t sources = info->Sources();
        const bool owned = OwnedByProject(tag);
        const size_t dot = name.rfind('.');
        const std::string label = dot == std::string::npos ? name : name.substr(dot + 1);

        ImGui::PushID(name.c_str());
        ImGui::TableNextRow();

        // Name
        ImGui::TableSetColumnIndex(0);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
        Tooltip(name.c_str());

        // Who declares it
        ImGui::TableSetColumnIndex(1);
        std::string from;
        if (sources & uint8_t(Core::TagSource::Project))
            from += "project";
        for (const Core::TagDeclaration& d : info->declarations)
            if (d.source == Core::TagSource::Plugin)
                from += (from.empty() ? "" : ", ") + d.owner;
        if (sources & uint8_t(Core::TagSource::Code))
            from += std::string(from.empty() ? "" : ", ") + "code";
        if (from.empty())
            ImGui::TextDisabled("(parent)");
        else
            ImGui::TextColored(kSourceColor, "%s", from.c_str());

        // Comment : the project's, editable when the project declares this tag (or only declares what is under it)
        ImGui::TableSetColumnIndex(2);
        Projects::TagSettings* settings = GetEngine().GetCurrentProject()->GetTagSettings();
        auto declared = settings->tags.find(name);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (owned) {
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer), "%s", declared != settings->tags.end() ? declared->second.c_str() : "");
            if (ImGui::InputTextWithHint("##Comment", "Comment", buffer, sizeof(buffer)))
                settings->tags[name] = buffer;
            if (ImGui::IsItemDeactivatedAfterEdit())
                ApplyTags();
        }
        else
            ImGui::TextDisabled("%s", info->Comment().c_str());

        // Actions
        ImGui::TableSetColumnIndex(3);
        if (ImGui::SmallButton(ICON_LC_PLUS)) {
            m_TagPopupTarget = name;
            m_TagNameBuffer[0] = '\0';
            m_TagError.clear();
            ImGui::OpenPopup("Add Child Tag");
        }
        Tooltip("Add a tag under this one");

        ImGui::SameLine();
        ImGui::BeginDisabled(!owned);
        if (ImGui::SmallButton(ICON_LC_PENCIL)) {
            m_TagPopupTarget = name;
            std::snprintf(m_TagNameBuffer, sizeof(m_TagNameBuffer), "%s", name.c_str());
            m_TagError.clear();
            ImGui::OpenPopup("Rename Tag");
        }
        Tooltip(owned ? "Rename this tag and what is under it. Worlds saved with the old name still load it (a redirect is added)."
                      : "A plugin or code declares this tag (or one under it) : rename it there.");

        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_LC_TRASH_2)) {
            m_TagPopupTarget = name;
            FindTagReferences(tag);
            ImGui::OpenPopup("Delete Tag");
        }
        Tooltip(owned ? "Delete this tag and what is under it" : "A plugin or code declares this tag (or one under it) : remove it there.");
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_LC_SEARCH)) {
            FindTagReferences(tag);
            ImGui::OpenPopup("Tag References");
        }
        Tooltip("Find what uses this tag (or one under it) in the worlds of the project");

        DrawTagPopups();

        if (open && !children.empty()) {
            for (Core::Tag child : children)
                DrawTagNode(child);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void ProjectSettingsPanel::DrawTagReferenceList()
    {
        if (m_TagReferences.empty()) {
            ImGui::TextDisabled("Used nowhere in the worlds of the project.");
            return;
        }
        ImGui::Text("Used %zu time(s) :", m_TagReferences.size());
        ImGui::BeginChild("##References", ImVec2(ImGui::GetFontSize() * 30.0f, ImGui::GetFontSize() * 10.0f), ImGuiChildFlags_Borders);
        for (const std::string& where : m_TagReferences)
            ImGui::BulletText("%s", where.c_str());
        ImGui::EndChild();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextDisabled("The other worlds are read from their files : a string of a component that reads as a query naming the tag is listed even when it is not a tag field.");
        ImGui::PopTextWrapPos();
    }

    void ProjectSettingsPanel::DrawTagPopups()
    {
        auto nameField = [this](const char* hint) {
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
            const bool enter = ImGui::InputTextWithHint("##Name", hint, m_TagNameBuffer, sizeof(m_TagNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
            if (!m_TagError.empty())
                ImGui::TextColored(kErrorColor, "%s", m_TagError.c_str());
            return enter;
        };

        if (ImGui::BeginPopupModal("Add Child Tag", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Under %s", m_TagPopupTarget.c_str());
            bool confirm = nameField("Name (Skeleton, or Undead.Skeleton)");
            confirm = ImGui::Button("Add") || confirm;
            if (confirm && AddTag(m_TagPopupTarget + "." + m_TagNameBuffer, &m_TagError))
                ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Rename Tag", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Rename %s (and what is under it) to", m_TagPopupTarget.c_str());
            bool confirm = nameField("Full new name");
            confirm = ImGui::Button("Rename") || confirm;
            if (confirm && RenameTag(m_TagPopupTarget, m_TagNameBuffer, &m_TagError))
                ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Delete Tag", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete %s and every tag under it ?", m_TagPopupTarget.c_str());
            DrawTagReferenceList();
            if (!m_TagReferences.empty())
                ImGui::TextWrapped("What carries it keeps it (and saves it), as a tag nobody declares : it matches only itself.");
            if (ImGui::Button(ICON_LC_TRASH_2 " Delete")) {
                DeleteTag(m_TagPopupTarget);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Tag References", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", m_TagReferencesOf.c_str());
            DrawTagReferenceList();
            if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void ProjectSettingsPanel::DrawTagsCategory()
    {
        Core::TagRegistry& registry = Core::TagRegistry::Get();
        Projects::TagSettings* settings = GetEngine().GetCurrentProject()->GetTagSettings();

        m_View.Section("Tags");

        if (m_View.BeginBlock("tags tag add new hierarchy tree"))
        {
            ImGui::TextWrapped("Hierarchical names that worlds, actors and components carry : Enemy.Undead.Skeleton is also an Enemy. "
                               "The project's own are saved in the project file ; the plugins' and the code's are shown, not editable.");

            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
            const bool enter = ImGui::InputTextWithHint("##NewTag", "New tag (Enemy.Undead.Skeleton)", m_NewTagBuffer, sizeof(m_NewTagBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button(ICON_LC_PLUS " Add") || enter) {
                if (AddTag(m_NewTagBuffer, &m_NewTagError)) {
                    m_NewTagBuffer[0] = '\0';
                    m_NewTagError.clear();
                }
            }
            if (!m_NewTagError.empty())
                ImGui::TextColored(kErrorColor, "%s", m_NewTagError.c_str());

            const std::vector<Core::Tag> roots = registry.GetRoots();
            if (roots.empty())
                ImGui::TextDisabled("No tag yet.");
            else if (ImGui::BeginTable("##TagTree", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthStretch, 3.0f);
                ImGui::TableSetupColumn("Declared by", ImGuiTableColumnFlags_WidthStretch, 1.5f);
                ImGui::TableSetupColumn("Comment", ImGuiTableColumnFlags_WidthStretch, 3.0f);
                // Four small buttons of one icon each
                const ImGuiStyle& style = ImGui::GetStyle();
                const float button = ImGui::CalcTextSize(ICON_LC_TRASH_2).x + style.FramePadding.x * 2.0f;
                ImGui::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed, button * 4.0f + style.ItemSpacing.x * 3.0f);
                ImGui::TableHeadersRow();

                for (Core::Tag root : roots)
                    DrawTagNode(root);

                ImGui::EndTable();
            }
        }

        m_View.Section("Redirects");

        if (m_View.BeginBlock("redirects redirect rename renamed old"))
        {
            if (settings->redirects.empty())
                ImGui::TextDisabled("None : a rename adds one per tag, so that the worlds saved before it find the tag again.");
            else
            {
                std::string removed;
                if (ImGui::BeginTable("##Redirects", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                {
                    for (const auto& [from, to] : settings->redirects)
                    {
                        ImGui::PushID(from.c_str());
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(from.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text(ICON_LC_LINK " %s", to.c_str());
                        ImGui::TableSetColumnIndex(2);
                        if (ImGui::SmallButton(ICON_LC_X))
                            removed = from;
                        Tooltip("Remove : the worlds that still say the old name will hold a tag nobody declares");
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                if (!removed.empty())
                {
                    settings->redirects.erase(removed);
                    ApplyTags();
                }
            }
        }
    }
}
