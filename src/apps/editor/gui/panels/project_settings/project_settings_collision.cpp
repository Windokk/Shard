#include "project_settings_panel.hpp"

#include "engine/assets/project/project.hpp"
#include "engine/core/collision/collision_classes.hpp"
#include "engine/core/collision/collision_diagnostics.hpp"
#include "engine/core/collision/collision_profile.hpp"
#include "engine/core/tags/tag_registry.hpp"
#include "engine/core/tags/tag_query.hpp"
#include "engine/physics/collision/collision_system.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/world/engine.hpp"

#include "apps/editor/gui/IconsLucide.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace Shard::Engine;
using Shard::Engine::Core::GetEngine;

namespace Shard::Editor::GUI{

    namespace {
        const ImVec4 kErrorColor(0.90f, 0.35f, 0.35f, 1.0f);
        const ImVec4 kWarnColor(0.95f, 0.70f, 0.30f, 1.0f);

        const char* kModes[] = { "Inherit", "Solid", "Sensor", "QueryOnly", "Disabled" };
        const char* kResponses[] = { "Inherit", "Ignore", "Overlap", "Block" };
        const char* kRuleResponses[] = { "Ignore", "Overlap", "Block" };
        const char* kEvents[] = { "OverlapBegin", "OverlapEnd", "Hit" };

        struct State {
            int profile = -1;
            int material = -1;
            int cellA = -1;
            int cellB = -1;
            std::string explanation;
            std::string identityText;
            int identityOf = -2;
            bool showDiagnostics = true;
        };

        State& S()
        {
            static State state;
            return state;
        }

        bool Text(const char* id, std::string& value, float width = 0.0f, const char* hint = nullptr)
        {
            char buffer[512];
            std::strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            if (width > 0.0f)
                ImGui::SetNextItemWidth(width);
            const bool changed = hint ? ImGui::InputTextWithHint(id, hint, buffer, sizeof(buffer)) : ImGui::InputText(id, buffer, sizeof(buffer));
            if (changed)
                value = buffer;
            return changed;
        }

        bool Choice(const char* id, int& index, const char* const* items, int count, float width = 0.0f)
        {
            if (width > 0.0f)
                ImGui::SetNextItemWidth(width);
            bool changed = false;
            if (ImGui::BeginCombo(id, items[std::clamp(index, 0, count - 1)])) {
                for (int i = 0; i < count; ++i)
                    if (ImGui::Selectable(items[i], i == index)) {
                        index = i;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            return changed;
        }

        int ModeIndex(const std::optional<Collision::Mode>& mode)
        {
            return mode ? int(*mode) + 1 : 0;
        }

        int ResponseIndex(const std::optional<Collision::Response>& response)
        {
            return response ? int(*response) + 1 : 0;
        }

        std::string UniqueName(const std::string& base, const std::vector<std::string>& taken)
        {
            if (std::find(taken.begin(), taken.end(), base) == taken.end())
                return base;
            for (int i = 2;; ++i) {
                const std::string candidate = base + std::to_string(i);
                if (std::find(taken.begin(), taken.end(), candidate) == taken.end())
                    return candidate;
            }
        }

        ImVec4 ResponseColor(Collision::Response response)
        {
            switch (response) {
                case Collision::Response::Block: return ImVec4(0.85f, 0.35f, 0.30f, 1.0f);
                case Collision::Response::Overlap: return ImVec4(0.35f, 0.65f, 0.90f, 1.0f);
                default: return ImVec4(0.40f, 0.40f, 0.40f, 1.0f);
            }
        }
    }

    void ProjectSettingsPanel::ApplyCollision()
    {
        auto project = GetEngine().GetCurrentProject();
        Projects::ApplyTagSettings(*project->GetTagSettings());
        Projects::ApplyCollisionSettings(*project->GetCollisionSettings());
        if (Physics::PhysicsManager* physics = GetEngine().GetPhysicsManager()) {
            Collision::InteractionTable& table = physics->Collision().Interactions();
            table.Rows() = project->GetCollisionSettings()->interactions;
            table.Touch();
        }
        m_LastDirtyCheck = -1.0;
    }

    void ProjectSettingsPanel::DrawCollisionCategory()
    {
        auto project = GetEngine().GetCurrentProject();
        Projects::CollisionSettings* settings = project->GetCollisionSettings();
        Projects::TagSettings* tags = project->GetTagSettings();
        State& state = S();
        bool changed = false;

        m_View.Section("Collision tags");
        if (m_View.BeginBlock("collision tags identity flag"))
        {
            ImGui::TextWrapped("The tags that say what a collider is. The mark applies to every tag below it. Other tags can still be tested by a rule, at each contact.");
            if (tags->tags.empty())
                ImGui::TextDisabled("No project tag yet : add some in Tags.");
            for (auto& [name, comment] : tags->tags)
            {
                bool marked = tags->collisionTags.count(name) != 0;
                ImGui::PushID(name.c_str());
                if (ImGui::Checkbox(name.c_str(), &marked))
                {
                    if (marked) tags->collisionTags.insert(name);
                    else tags->collisionTags.erase(name);
                    changed = true;
                }
                ImGui::PopID();
            }
        }

        m_View.Section("Profiles");
        if (m_View.BeginBlock("collision profiles rules identity mode default response base"))
        {
            if (state.profile >= int(settings->profiles.size()))
                state.profile = -1;

            if (ImGui::Button(ICON_LC_PLUS " Add profile"))
            {
                std::vector<std::string> taken = Collision::ProfileRegistry::Get().Names();
                Collision::Profile profile;
                profile.name = UniqueName("Profile", taken);
                profile.defaultResponse = Collision::Response::Ignore;
                settings->profiles.push_back(profile);
                state.profile = int(settings->profiles.size()) - 1;
                changed = true;
            }

            ImGui::BeginChild("##ProfileList", ImVec2(ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 18.0f), true);
            for (size_t i = 0; i < settings->profiles.size(); ++i)
                if (ImGui::Selectable(settings->profiles[i].name.c_str(), state.profile == int(i)))
                    state.profile = int(i);
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("##ProfileEditor", ImVec2(0.0f, ImGui::GetFontSize() * 18.0f), true);
            if (state.profile >= 0)
            {
                Collision::Profile& profile = settings->profiles[state.profile];
                ImGui::PushID(state.profile);
                const std::string before = profile.name;
                if (Text("Name", profile.name))
                {
                    for (Collision::Profile& other : settings->profiles)
                        if (other.base == before) other.base = profile.name;
                    changed = true;
                }
                std::string base = profile.base;
                if (ImGui::BeginCombo("Base", base.empty() ? "None" : base.c_str()))
                {
                    if (ImGui::Selectable("None", base.empty())) { profile.base.clear(); changed = true; }
                    for (const std::string& name : Collision::ProfileRegistry::Get().Names())
                        if (name != profile.name && ImGui::Selectable(name.c_str(), name == base)) { profile.base = name; changed = true; }
                    ImGui::EndCombo();
                }
                if (state.identityOf != state.profile)
                {
                    state.identityText = profile.identity.ToString();
                    state.identityOf = state.profile;
                }
                if (Text("Identity", state.identityText, 0.0f, "Enemy.Undead.Skeleton, Mob"))
                {
                    Core::TagContainer identity;
                    std::string item;
                    for (size_t i = 0; i <= state.identityText.size(); ++i)
                    {
                        const char c = i < state.identityText.size() ? state.identityText[i] : ',';
                        if (c == ',' || c == ' ')
                        {
                            if (!item.empty() && Core::TagRegistry::IsValidName(item, nullptr))
                                identity.Add(Core::TagRegistry::Get().Resolve(item));
                            item.clear();
                        }
                        else item += c;
                    }
                    profile.identity = identity;
                    changed = true;
                }
                int mode = ModeIndex(profile.mode);
                if (Choice("Mode", mode, kModes, 5))
                {
                    profile.mode = mode == 0 ? std::nullopt : std::optional<Collision::Mode>(Collision::Mode(mode - 1));
                    changed = true;
                }
                int fallback = ResponseIndex(profile.defaultResponse);
                if (Choice("Default response", fallback, kResponses, 4))
                {
                    profile.defaultResponse = fallback == 0 ? std::nullopt : std::optional<Collision::Response>(Collision::Response(fallback - 1));
                    changed = true;
                }
                if (ImGui::BeginCombo("Material", profile.material.empty() ? "None" : profile.material.c_str()))
                {
                    if (ImGui::Selectable("None", profile.material.empty())) { profile.material.clear(); changed = true; }
                    for (const std::string& name : Collision::ProfileRegistry::Get().MaterialNames())
                        if (ImGui::Selectable(name.c_str(), name == profile.material)) { profile.material = name; changed = true; }
                    ImGui::EndCombo();
                }
                bool custom = profile.events.has_value();
                if (ImGui::Checkbox("Custom event options", &custom))
                {
                    profile.events = custom ? std::optional<Collision::EventOptions>(Collision::EventOptions()) : std::nullopt;
                    changed = true;
                }
                if (profile.events)
                {
                    changed |= ImGui::Checkbox("Overlap events", &profile.events->overlaps);
                    changed |= ImGui::Checkbox("Hit events", &profile.events->hits);
                    changed |= ImGui::DragFloat("Min impact speed", &profile.events->minImpactSpeed, 0.05f, 0.0f, 1000.0f);
                }

                ImGui::SeparatorText("Rules, first match wins");
                int removed = -1, moveUp = -1;
                for (size_t i = 0; i < profile.rules.size(); ++i)
                {
                    Collision::Rule& rule = profile.rules[i];
                    ImGui::PushID(int(i));
                    ImGui::Text("%zu", i + 1);
                    ImGui::SameLine();
                    std::string text = rule.target.ToString();
                    if (Text("##target", text, ImGui::GetFontSize() * 14.0f, "Enemy | Prop"))
                    {
                        Core::TagQuery parsed;
                        std::string error;
                        if (Core::TagQuery::TryParse(text, parsed, &error)) { rule.target = parsed; changed = true; }
                    }
                    ImGui::SameLine();
                    int response = int(rule.response);
                    if (Choice("##response", response, kRuleResponses, 3, ImGui::GetFontSize() * 7.0f)) { rule.response = Collision::Response(response); changed = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_LC_ARROW_UP) && i > 0) moveUp = int(i);
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_LC_TRASH_2)) removed = int(i);
                    ImGui::PopID();
                }
                if (moveUp > 0) { std::swap(profile.rules[moveUp], profile.rules[moveUp - 1]); changed = true; }
                if (removed >= 0) { profile.rules.erase(profile.rules.begin() + removed); changed = true; }
                if (ImGui::Button(ICON_LC_PLUS " Add rule"))
                {
                    profile.rules.push_back({ Core::TagQuery(), Collision::Response::Block });
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_LC_TRASH_2 " Delete profile"))
                {
                    settings->profiles.erase(settings->profiles.begin() + state.profile);
                    state.profile = -1;
                    state.identityOf = -2;
                    changed = true;
                }
                ImGui::PopID();
            }
            else
            {
                ImGui::TextDisabled("Select a profile.");
            }
            ImGui::EndChild();
        }

        m_View.Section("Interaction table");
        if (m_View.BeginBlock("interaction table actions hit overlap rows"))
        {
            ImGui::TextWrapped("What happens when two kinds of things meet. The most specific row wins, a surface looks one level up the tag tree when it has no row.");
            if (ImGui::Button(ICON_LC_PLUS " Add row"))
            {
                settings->interactions.emplace_back();
                changed = true;
            }
            int removed = -1;
            for (size_t i = 0; i < settings->interactions.size(); ++i)
            {
                Collision::InteractionRow& row = settings->interactions[i];
                ImGui::PushID(int(i));
                ImGui::Separator();
                changed |= Text("##rowname", row.name, ImGui::GetFontSize() * 10.0f, "Name");
                ImGui::SameLine();
                std::string a = row.a.ToString();
                if (Text("##a", a, ImGui::GetFontSize() * 10.0f, "A tags"))
                {
                    Core::TagQuery parsed;
                    if (Core::TagQuery::TryParse(a, parsed)) { row.a = parsed; changed = true; }
                }
                ImGui::SameLine();
                std::string b = row.b.ToString();
                if (Text("##b", b, ImGui::GetFontSize() * 10.0f, "B tags"))
                {
                    Core::TagQuery parsed;
                    if (Core::TagQuery::TryParse(b, parsed)) { row.b = parsed; changed = true; }
                }
                ImGui::SameLine();
                int event = int(row.event);
                if (Choice("##event", event, kEvents, 3, ImGui::GetFontSize() * 8.0f)) { row.event = Collision::EventKind(event); changed = true; }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
                changed |= ImGui::DragFloat("##minspeed", &row.minSpeed, 0.05f, 0.0f, 1000.0f, "min %.1f");
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_LC_TRASH_2)) removed = int(i);

                int removedAction = -1;
                for (size_t k = 0; k < row.actions.size(); ++k)
                {
                    Collision::Action& action = row.actions[k];
                    ImGui::PushID(int(k));
                    ImGui::Indent();
                    changed |= Text("##type", action.type, ImGui::GetFontSize() * 10.0f, "Action");
                    std::string params;
                    for (const auto& [key, value] : action.params) params += (params.empty() ? "" : ", ") + key + "=" + value;
                    ImGui::SameLine();
                    if (Text("##params", params, ImGui::GetFontSize() * 18.0f, "key=value, key=value"))
                    {
                        action.params.clear();
                        size_t start = 0;
                        while (start <= params.size())
                        {
                            size_t end = params.find(',', start);
                            if (end == std::string::npos) end = params.size();
                            std::string part = params.substr(start, end - start);
                            part.erase(0, part.find_first_not_of(' '));
                            const size_t eq = part.find('=');
                            if (eq != std::string::npos) action.params.emplace_back(part.substr(0, eq), part.substr(eq + 1));
                            start = end + 1;
                        }
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_LC_X)) removedAction = int(k);
                    ImGui::Unindent();
                    ImGui::PopID();
                }
                if (removedAction >= 0) { row.actions.erase(row.actions.begin() + removedAction); changed = true; }
                ImGui::Indent();
                if (ImGui::SmallButton(ICON_LC_PLUS " Action"))
                {
                    row.actions.emplace_back();
                    changed = true;
                }
                ImGui::Unindent();
                ImGui::PopID();
            }
            if (removed >= 0) { settings->interactions.erase(settings->interactions.begin() + removed); changed = true; }
        }

        if (changed)
            ApplyCollision();

        m_View.Section("Matrix");
        if (m_View.BeginBlock("collision matrix profiles block overlap ignore explain"))
        {
            ImGui::TextWrapped("Every profile against every other, for two moving colliders. Read only : click a cell to see why.");
            Collision::ProfileRegistry& registry = Collision::ProfileRegistry::Get();
            const std::vector<std::string> names = registry.Names();
            Collision::ClassTable table;
            std::vector<Collision::ClassId> classes;
            for (const std::string& name : names)
            {
                Collision::ResolvedProfile resolved;
                classes.push_back(registry.Resolve(name, resolved) ? table.Acquire(resolved, true) : Collision::kInvalidClass);
            }
            if (!names.empty() && ImGui::BeginTable("##Matrix", int(names.size()) + 1, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, ImVec2(0.0f, ImGui::GetFontSize() * 16.0f)))
            {
                ImGui::TableSetupScrollFreeze(1, 1);
                ImGui::TableSetupColumn("");
                for (const std::string& name : names)
                    ImGui::TableSetupColumn(name.c_str());
                ImGui::TableHeadersRow();
                for (size_t row = 0; row < names.size(); ++row)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(names[row].c_str());
                    for (size_t column = 0; column < names.size(); ++column)
                    {
                        ImGui::TableSetColumnIndex(int(column) + 1);
                        if (classes[row] == Collision::kInvalidClass || classes[column] == Collision::kInvalidClass)
                        {
                            ImGui::TextColored(kErrorColor, "?");
                            continue;
                        }
                        const Collision::Response response = table.PairResponse(classes[row], classes[column]);
                        ImGui::PushStyleColor(ImGuiCol_Text, ResponseColor(response));
                        ImGui::PushID(int(row * names.size() + column));
                        const char* glyph = response == Collision::Response::Block ? "B" : response == Collision::Response::Overlap ? "O" : "-";
                        if (ImGui::Selectable(glyph, state.cellA == int(row) && state.cellB == int(column), 0, ImVec2(ImGui::GetFontSize() * 1.5f, 0.0f)))
                        {
                            state.cellA = int(row);
                            state.cellB = int(column);
                            const Collision::Decision d = table.Decide(classes[row], classes[column], nullptr, nullptr, true);
                            state.explanation.clear();
                            for (const std::string& line : d.reasons) state.explanation += line + "\n";
                        }
                        ImGui::PopID();
                        ImGui::PopStyleColor();
                    }
                }
                ImGui::EndTable();
            }
            if (state.cellA >= 0 && state.cellA < int(names.size()) && state.cellB >= 0 && state.cellB < int(names.size()))
            {
                ImGui::SeparatorText((names[state.cellA] + " / " + names[state.cellB]).c_str());
                ImGui::TextWrapped("%s", state.explanation.c_str());
            }
        }

        m_View.Section("Warnings");
        if (m_View.BeginBlock("collision warnings diagnostics dead rule shadowed isolated"))
        {
            const std::vector<Collision::Diagnostic> found = Collision::Analyze(Collision::ProfileRegistry::Get());
            if (found.empty())
                ImGui::TextDisabled("Nothing to report.");
            for (const Collision::Diagnostic& d : found)
            {
                ImGui::TextColored(d.kind == Collision::DiagnosticKind::BrokenProfile ? kErrorColor : kWarnColor, "%s", Collision::ToString(d.kind));
                ImGui::SameLine();
                ImGui::TextWrapped("%s", d.message.c_str());
                if (!d.profile.empty())
                {
                    ImGui::SameLine();
                    ImGui::PushID(d.message.c_str());
                    if (ImGui::SmallButton("Go to"))
                        for (size_t i = 0; i < settings->profiles.size(); ++i)
                            if (settings->profiles[i].name == d.profile) { state.profile = int(i); state.identityOf = -2; }
                    ImGui::PopID();
                }
            }
        }
    }
}
