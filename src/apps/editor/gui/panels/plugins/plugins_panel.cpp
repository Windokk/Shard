#include "plugins_panel.hpp"

#include "engine/plugins/manager.hpp"

#include "imgui/imgui.h"

#include <cstdio>

namespace Shard::Editor::GUI{

    using namespace Shard::Engine::Plugins;

    namespace {
        ImVec4 StateColor(PluginState state)
        {
            switch(state){
                case PluginState::Enabled:     return ImVec4(0.30f, 0.80f, 0.40f, 1.0f);
                case PluginState::Disabled:    return ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
                case PluginState::Failed:      return ImVec4(0.95f, 0.35f, 0.30f, 1.0f);
                case PluginState::Initialized: return ImVec4(0.90f, 0.75f, 0.30f, 1.0f);
                default:                       return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
            }
        }

        const char* KindLabel(const PluginManifest& m)
        {
            return m.moduleKind != ModuleKind::None ? ModuleKindName(m.moduleKind) : PluginKindName(m.kind);
        }
    }

    void PluginsPanel::Draw(bool* open)
    {
        if(!ImGui::Begin("Plugins", open)){
            ImGui::End();
            return;
        }

        PluginManager& manager = PluginManager::Get();
        if(!manager.IsStarted()){
            ImGui::TextDisabled("The plugin system is not running.");
            ImGui::End();
            return;
        }

        const uint64_t hash = manager.SimulationPluginHash();
        char hashText[32];
        std::snprintf(hashText, sizeof(hashText), "%016llx", static_cast<unsigned long long>(hash));
        ImGui::Text("%zu plugins", manager.Plugins().size());
        ImGui::SameLine();
        ImGui::TextDisabled("simulation set %s", hashText);
        if(ImGui::IsItemHovered())
            ImGui::SetTooltip("What a replay records and a server compares : the plugins that change the simulation, with their versions and content.");

        if(!manager.Config().lockfilePath.empty()){
            ImGui::SameLine();
            if(ImGui::SmallButton("Write plugins.lock")){
                m_LastMessageIsError = !manager.WriteLockfile(manager.Config().lockfilePath);
                m_LastMessage = m_LastMessageIsError ? "Could not write plugins.lock" : "Wrote plugins.lock";
            }
        }

        if(!m_LastMessage.empty())
            ImGui::TextColored(m_LastMessageIsError ? ImVec4(0.95f, 0.35f, 0.30f, 1.0f) : ImVec4(0.30f, 0.80f, 0.40f, 1.0f), "%s", m_LastMessage.c_str());

        ImGui::Separator();
        DrawList();
        ImGui::Separator();
        DrawDetails();
        DrawProblems();

        ImGui::End();
    }

    void PluginsPanel::DrawList()
    {
        PluginManager& manager = PluginManager::Get();
        const float height = ImGui::GetContentRegionAvail().y * 0.5f;
        if(!ImGui::BeginTable("##plugins", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0, height)))
            return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("Plugin", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();

        for(const auto& record : manager.Plugins()){
            const PluginRecord& r = *record;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%u", r.owner);
            ImGui::TableNextColumn();
            const bool selected = m_Selected == r.manifest.id;
            if(ImGui::Selectable((r.manifest.name.empty() ? r.manifest.id : r.manifest.name).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                m_Selected = r.manifest.id;
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", r.manifest.id.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.manifest.version.ToString().c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(KindLabel(r.manifest));
            ImGui::TableNextColumn();
            ImGui::TextColored(StateColor(r.state), "%s", PluginStateName(r.state));
            ImGui::TableNextColumn();
            if(r.IsRunning())
                ImGui::Text("%.2f", r.systemsMsAverage + r.frameMsAverage);
        }
        ImGui::EndTable();
    }

    void PluginsPanel::DrawDetails()
    {
        PluginManager& manager = PluginManager::Get();
        PluginRecord* r = m_Selected.empty() ? nullptr : manager.Find(m_Selected);
        if(!r){
            ImGui::TextDisabled("Select a plugin to see what it registered and to control it.");
            return;
        }

        const bool enginePart = r->isStatic && r->manifest.id.compare(0, 6, "shard.") == 0;
        std::string error;

        ImGui::BeginDisabled(enginePart);
        if(r->state == PluginState::Enabled || r->state == PluginState::Initialized){
            if(ImGui::Button("Disable")){
                m_LastMessageIsError = !manager.Disable(r->manifest.id, true, &error);
                m_LastMessage = m_LastMessageIsError ? error : r->manifest.id + " disabled";
            }
        } else {
            if(ImGui::Button(r->state == PluginState::Failed ? "Try again" : "Enable")){
                m_LastMessageIsError = !manager.Enable(r->manifest.id, &error);
                m_LastMessage = m_LastMessageIsError ? error : r->manifest.id + " enabled";
            }
        }
        ImGui::EndDisabled();

        const bool canReload = manager.Config().allowHotReload && !r->isStatic && r->manifest.moduleKind != ModuleKind::Datapack && !r->legacy;
        ImGui::SameLine();
        ImGui::BeginDisabled(!canReload);
        if(ImGui::Button("Reload")){
            m_LastMessageIsError = !manager.Reload(r->manifest.id, &error);
            m_LastMessage = m_LastMessageIsError ? error : r->manifest.id + " reloaded";
        }
        ImGui::EndDisabled();
        if(!canReload && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Only a library built with SHARD_PLUGIN can be reloaded, and only while the editor is not deterministic.");

        ImGui::BeginChild("##details", ImVec2(0, 0), ImGuiChildFlags_None);
        ImGui::TextUnformatted(manager.DescribePlugin(r->manifest.id).c_str());
        ImGui::EndChild();
    }

    void PluginsPanel::DrawProblems()
    {
        PluginManager& manager = PluginManager::Get();
        bool any = false;
        for(const Rejection& rej : manager.Rejections()){
            if(rej.kind == Rejection::Kind::WrongHost || rej.kind == Rejection::Kind::Duplicate) continue;
            any = true;
            break;
        }
        const bool lockProblems = !manager.LockfileProblems().empty();
        const auto conflicts = manager.Content().FindConflicts();
        if(!any && !lockProblems && conflicts.empty())
            return;

        ImGui::Separator();
        if(ImGui::CollapsingHeader("Problems", ImGuiTreeNodeFlags_DefaultOpen)){
            for(const Rejection& rej : manager.Rejections()){
                if(rej.kind == Rejection::Kind::WrongHost || rej.kind == Rejection::Kind::Duplicate) continue;
                ImGui::TextColored(ImVec4(0.90f, 0.75f, 0.30f, 1.0f), "%s %s not loaded : %s", rej.id.c_str(), rej.version.c_str(), rej.reason.c_str());
            }
            for(const std::string& line : manager.LockfileProblems())
                ImGui::TextColored(ImVec4(0.90f, 0.75f, 0.30f, 1.0f), "plugins.lock : %s", line.c_str());
            for(const ContentConflict& c : conflicts)
                ImGui::TextColored(ImVec4(0.90f, 0.75f, 0.30f, 1.0f), "%s : %s is used, %s is ignored (no content.overrides)", c.logicalPath.c_str(), c.keptFrom.c_str(), c.rejectedFrom.c_str());
        }
    }
}
