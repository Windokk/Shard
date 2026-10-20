#include "tag_widgets.hpp"

#include "engine/core/tags/tag_registry.hpp"

#include "apps/editor/gui/IconsLucide.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <string>
#include <unordered_map>

using namespace Shard::Engine;

namespace Shard::Editor::GUI::TagWidgets{

    namespace {
        const ImVec4 kUnknownColor(0.95f, 0.70f, 0.30f, 1.0f);
        const ImVec4 kErrorColor(0.90f, 0.35f, 0.35f, 1.0f);

        /// The search box of the tree : one for every tree, it is only ever open in one popup
        char gSearch[128] = "";

        bool ContainsNoCase(const std::string& text, const char* search)
        {
            const std::string needle = search;
            auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
            return it != text.end();
        }

        void TagTooltip(Core::Tag tag)
        {
            if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                return;

            const Core::TagInfo* info = Core::TagRegistry::Get().GetInfo(tag);
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(tag.ToString().c_str());
            if (!info) {
                ImGui::TextColored(kUnknownColor, ICON_LC_TRIANGLE_ALERT " Not declared : the project, a plugin or code no longer declares it. It is kept, and matches only itself.");
            }
            else {
                if (!info->Comment().empty())
                    ImGui::TextDisabled("%s", info->Comment().c_str());
                for (const Core::TagDeclaration& d : info->declarations) {
                    if (d.source == Core::TagSource::Plugin)
                        ImGui::TextDisabled(ICON_LC_PLUG " Declared by the plugin %s", d.owner.c_str());
                    else if (d.source == Core::TagSource::Code)
                        ImGui::TextDisabled(ICON_LC_CODE " Declared in code");
                }
            }
            ImGui::EndTooltip();
        }

        /// One branch of the tree. True when a tag was clicked.
        bool TagNode(Core::Tag tag, Core::Tag& picked, const Core::TagContainer* marked)
        {
            const Core::TagRegistry& registry = Core::TagRegistry::Get();
            const Core::TagInfo* info = registry.GetInfo(tag);
            if (!info)
                return false;

            const std::string& name = info->name;
            const size_t dot = name.rfind('.');
            const std::string label = (dot == std::string::npos ? name : name.substr(dot + 1));
            const bool isMarked = marked && marked->HasTagExact(tag);

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
            if (info->children.empty())
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (isMarked)
                flags |= ImGuiTreeNodeFlags_Selected;

            const std::string text = std::string(isMarked ? ICON_LC_CHECK " " : "") + label + "##" + name;
            const bool open = ImGui::TreeNodeEx(text.c_str(), flags);
            bool clicked = false;
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                picked = tag;
                clicked = true;
            }
            TagTooltip(tag);

            if (open && !info->children.empty()) {
                for (Core::Tag child : info->children)
                    clicked = TagNode(child, picked, marked) || clicked;
                ImGui::TreePop();
            }
            return clicked;
        }

        /// The chip of one tag. True when it was clicked.
        bool Chip(Core::Tag tag, bool removable)
        {
            const std::string name = tag.ToString();
            const bool known = Core::TagRegistry::Get().IsKnown(tag);
            const std::string label = (known ? "" : ICON_LC_TRIANGLE_ALERT " ") + name + (removable ? "  " ICON_LC_X : "") + "##chip" + name;

            // Wraps to the next line when it does not fit on this one
            const ImGuiStyle& style = ImGui::GetStyle();
            const float width = ImGui::CalcTextSize(label.c_str(), nullptr, true).x + style.FramePadding.x * 2.0f;
            const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            ImGui::SameLine();
            if (ImGui::GetCursorScreenPos().x + width > right)
                ImGui::NewLine();

            if (!known)
                ImGui::PushStyleColor(ImGuiCol_Text, kUnknownColor);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
            const bool clicked = ImGui::SmallButton(label.c_str());
            ImGui::PopStyleVar();
            if (!known)
                ImGui::PopStyleColor();
            TagTooltip(tag);
            return clicked;
        }

        /// What a query field shows while it is edited, by ImGui id (the query itself only changes when the text is valid)
        struct QueryDraft {
            std::string text;
            std::string error;
            std::string unknown;        // the names nobody declares
        };
        std::unordered_map<ImGuiID, QueryDraft> gQueryDrafts;

        void CheckDraft(QueryDraft& draft)
        {
            Core::TagQuery parsed;
            draft.error.clear();
            draft.unknown.clear();
            if (!Core::TagQuery::TryParse(draft.text, parsed, &draft.error, false))
                return;
            for (Core::Tag tag : parsed.GetTags())
                if (!Core::TagRegistry::Get().IsKnown(tag)) {
                    // The text of a tag nobody declares is not remembered while typing : the draft has it
                    draft.unknown = "Not declared : a name of this query matches only itself.";
                    break;
                }
        }
    }

    bool TagTree(Core::Tag& picked, const Core::TagContainer* marked)
    {
        const Core::TagRegistry& registry = Core::TagRegistry::Get();

        if (ImGui::IsWindowAppearing()) {
            gSearch[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
        ImGui::InputTextWithHint("##TagSearch", ICON_LC_SEARCH "  Search tags...", gSearch, sizeof(gSearch));

        bool clicked = false;
        ImGui::BeginChild("##TagTree", ImVec2(ImGui::GetFontSize() * 18.0f, ImGui::GetFontSize() * 16.0f), ImGuiChildFlags_None);

        const std::vector<Core::Tag> roots = registry.GetRoots();
        if (roots.empty())
            ImGui::TextDisabled("No tag yet : add some in Project Settings > Tags");

        if (gSearch[0] != '\0') {
            // Searching : the matching tags, by their full name
            for (Core::Tag tag : registry.GetAll()) {
                const std::string name = tag.ToString();
                if (!ContainsNoCase(name, gSearch))
                    continue;
                const bool isMarked = marked && marked->HasTagExact(tag);
                if (ImGui::Selectable((std::string(isMarked ? ICON_LC_CHECK " " : "") + name).c_str(), isMarked)) {
                    picked = tag;
                    clicked = true;
                }
                TagTooltip(tag);
            }
        }
        else {
            for (Core::Tag root : roots)
                clicked = TagNode(root, picked, marked) || clicked;
        }

        ImGui::EndChild();
        return clicked;
    }

    bool TagContainerEditor(const char* id, Core::TagContainer& tags)
    {
        bool changed = false;
        ImGui::PushID(id);
        ImGui::BeginGroup();

        // Sorted by name, like the file : the order of the container is the hashes'
        std::vector<Core::Tag> sorted(tags.begin(), tags.end());
        std::sort(sorted.begin(), sorted.end(), [](Core::Tag a, Core::Tag b) { return a.ToString() < b.ToString(); });

        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(ICON_LC_TAG);
        Core::Tag removed;
        for (Core::Tag tag : sorted)
            if (Chip(tag, true))
                removed = tag;
        if (removed.IsValid())
            changed = tags.Remove(removed);

        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_LC_PLUS "##AddTag"))
            ImGui::OpenPopup("##AddTagPopup");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(tags.IsEmpty() ? "Add a tag" : "Add a tag (click a tag to remove it)");

        if (ImGui::BeginPopup("##AddTagPopup")) {
            Core::Tag picked;
            if (TagTree(picked, &tags)) {
                // Picking a tag that is there removes it : the tree shows a check on the ones the container has
                changed = tags.HasTagExact(picked) ? tags.Remove(picked) : tags.Add(picked);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::EndGroup();
        ImGui::PopID();
        return changed;
    }

    bool TagPicker(const char* id, Core::Tag& tag)
    {
        bool changed = false;
        ImGui::PushID(id);

        const bool known = !tag.IsValid() || Core::TagRegistry::Get().IsKnown(tag);
        const std::string label = tag.IsValid() ? std::string(known ? ICON_LC_TAG " " : ICON_LC_TRIANGLE_ALERT " ") + tag.ToString() : std::string("(none)");
        if (!known)
            ImGui::PushStyleColor(ImGuiCol_Text, kUnknownColor);
        if (ImGui::Button((label + "##TagPicker").c_str(), ImVec2(-FLT_MIN, 0)))
            ImGui::OpenPopup("##TagPickerPopup");
        if (!known)
            ImGui::PopStyleColor();
        if (tag.IsValid())
            TagTooltip(tag);

        if (ImGui::BeginPopup("##TagPickerPopup")) {
            if (tag.IsValid() && ImGui::MenuItem(ICON_LC_X " None")) {
                tag = Core::Tag();
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            Core::Tag picked;
            const Core::TagContainer current = tag.IsValid() ? Core::TagContainer{ tag } : Core::TagContainer();
            if (TagTree(picked, &current)) {
                changed = picked != tag;
                tag = picked;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
        return changed;
    }

    bool TagQueryInput(const char* id, Core::TagQuery& query)
    {
        bool changed = false;
        ImGui::PushID(id);
        const ImGuiID key = ImGui::GetID("##Query");

        auto it = gQueryDrafts.find(key);
        const bool editing = it != gQueryDrafts.end();
        QueryDraft shown = editing ? it->second : QueryDraft{ query.ToString(), {}, {} };
        if (!editing)
            CheckDraft(shown);

        const float buttonWidth = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(-(buttonWidth + ImGui::GetStyle().ItemSpacing.x));

        const bool invalid = !shown.error.empty();
        if (invalid)
            ImGui::PushStyleColor(ImGuiCol_Border, kErrorColor);
        if (invalid)
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), "%s", shown.text.c_str());
        const bool edited = ImGui::InputTextWithHint("##Query", "Everything  (e.g. Enemy & !Enemy.Boss)", buffer, sizeof(buffer));

        if (invalid) {
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }

        if (edited) {
            QueryDraft& draft = gQueryDrafts[key];
            draft.text = buffer;
            CheckDraft(draft);
            shown = draft;
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::BeginTooltip();
            if (!shown.error.empty())
                ImGui::TextColored(kErrorColor, "%s", shown.error.c_str());
            else if (!shown.unknown.empty())
                ImGui::TextColored(kUnknownColor, "%s", shown.unknown.c_str());
            ImGui::TextDisabled("& and, | or, ! not, ( ) group. A name is that tag or one under it, =Name that tag only. Empty : everything.");
            ImGui::EndTooltip();
        }

        // Applied when the field is left : the text is read again, this time keeping the names it holds
        auto commit = [&]() {
            auto draft = gQueryDrafts.find(key);
            if (draft == gQueryDrafts.end())
                return;
            Core::TagQuery parsed;
            if (draft->second.error.empty() && Core::TagQuery::TryParse(draft->second.text, parsed) && parsed != query) {
                query = std::move(parsed);
                changed = true;
            }
            gQueryDrafts.erase(draft);
        };
        if (ImGui::IsItemDeactivated())
            commit();

        ImGui::SameLine();
        if (ImGui::Button(ICON_LC_PLUS "##InsertTag", ImVec2(buttonWidth, 0)))
            ImGui::OpenPopup("##InsertTagPopup");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Insert a tag");

        if (ImGui::BeginPopup("##InsertTagPopup")) {
            Core::Tag picked;
            if (TagTree(picked, nullptr)) {
                // Added as one more term of an `&`, or alone in an empty query
                const std::string text = query.ToString();
                Core::TagQuery parsed;
                if (Core::TagQuery::TryParse(text.empty() ? picked.ToString() : "(" + text + ") & " + picked.ToString(), parsed)) {
                    query = std::move(parsed);
                    changed = true;
                }
                gQueryDrafts.erase(key);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
        return changed;
    }
}
