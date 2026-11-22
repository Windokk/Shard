#include "collision_debug_panel.hpp"

#include "apps/editor/gui/main_window.hpp"
#include "apps/editor/debug/collision_debug_state.hpp"
#include "apps/editor/gui/IconsLucide.h"

#include "engine/core/collision/collision_recorder.hpp"
#include "engine/physics/collision/collision_system.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"

#include "imgui/imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace Shard::Engine;
namespace EngineCore = Shard::Engine::Core;

namespace Shard::Editor::GUI{

    namespace {
        const char* kRecordKinds[] = { "Contact", "Event", "Query", "Override" };

        Physics::CollisionSystem* System()
        {
            Physics::PhysicsManager* physics = EngineCore::GetEngine().GetPhysicsManager();
            return physics ? &physics->Collision() : nullptr;
        }

        JPH::BodyID BodyOf(const std::shared_ptr<Objects::Actor>& actor)
        {
            if (!actor)
                return JPH::BodyID();
            auto body = actor->GetComponent<Objects::Components::PhysicsBody>();
            return body ? body->GetBodyID() : JPH::BodyID();
        }
    }

    void CollisionDebugPanel::Draw(bool* open)
    {
        if (!ImGui::Begin("Collision", open))
        {
            ImGui::End();
            return;
        }

        Physics::CollisionSystem* system = System();
        if (!system)
        {
            ImGui::TextDisabled("Physics is not running.");
            ImGui::End();
            return;
        }

        if (!paused)
            system->SetRecording(true);

        if (ImGui::BeginTabBar("##CollisionTabs"))
        {
            if (ImGui::BeginTabItem("Explain")) { DrawExplain(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Recorder")) { DrawRecorder(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Live")) { DrawLive(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    namespace {
        ImVec4 ResponseColor(Collision::Response response)
        {
            switch (response)
            {
                case Collision::Response::Block: return ImVec4(0.90f, 0.40f, 0.35f, 1.0f);
                case Collision::Response::Overlap: return ImVec4(0.95f, 0.70f, 0.30f, 1.0f);
                default: return ImVec4(0.55f, 0.58f, 0.62f, 1.0f);
            }
        }

        const char* ResponseIcon(Collision::Response response)
        {
            switch (response)
            {
                case Collision::Response::Block: return ICON_LC_SHIELD;
                case Collision::Response::Overlap: return ICON_LC_GHOST;
                default: return ICON_LC_BAN;
            }
        }

        const char* ResponseMeaning(Collision::Response response)
        {
            switch (response)
            {
                case Collision::Response::Block: return "They collide. Contacts are solved and they push each other.";
                case Collision::Response::Overlap: return "They pass through each other and report overlap events.";
                default: return "They do not interact.";
            }
        }

        bool StartsWith(const std::string& text, const char* prefix)
        {
            return text.rfind(prefix, 0) == 0;
        }

        void Badge(const char* text, const ImVec4& color)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x, color.y, color.z, 0.25f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x, color.y, color.z, 0.25f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x, color.y, color.z, 0.25f));
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::SmallButton(text);
            ImGui::PopStyleColor(4);
        }

        void DrawSideCard(const char* heading, const Physics::ExplainedSide& side, const std::vector<std::string>& reasons, const char* reasonPrefix)
        {
            ImGui::TextDisabled("%s", heading);
            if (!side.hasProfile)
            {
                ImGui::TextWrapped("No collision profile. This is not a body of an actor.");
                return;
            }
            ImGui::TextUnformatted(side.actor.c_str());
            ImGui::TextDisabled("Profile");
            ImGui::SameLine();
            ImGui::TextUnformatted(side.profile.c_str());

            Badge(Collision::ToString(side.mode), ImVec4(0.45f, 0.65f, 0.95f, 1.0f));
            ImGui::SameLine();
            Badge(side.moving ? "Moving" : "Static", ImVec4(0.60f, 0.60f, 0.65f, 1.0f));
            if (!side.material.empty())
            {
                ImGui::SameLine();
                Badge(side.material.c_str(), ImVec4(0.50f, 0.78f, 0.40f, 1.0f));
            }

            for (const std::string& entry : side.overrides)
                ImGui::BulletText("override %s", entry.c_str());

            const size_t skip = std::strlen(reasonPrefix);
            for (const std::string& reason : reasons)
                if (StartsWith(reason, reasonPrefix))
                {
                    ImGui::Spacing();
                    ImGui::TextWrapped("%s", reason.c_str() + skip);
                }
        }
    }

    void CollisionDebugPanel::DrawExplain()
    {
        Physics::CollisionSystem* system = System();
        const auto& selection = parent ? parent->GetSelectedActors() : std::vector<std::shared_ptr<Objects::Actor>>();
        if (selection.size() != 2)
        {
            ImGui::Spacing();
            ImGui::TextDisabled(ICON_LC_MOUSE_POINTER_CLICK " Select two actors in the world tree to see why they collide or not.");
            if (selection.size() == 1)
                ImGui::TextDisabled("One is selected: pick another with Ctrl.");
            return;
        }
        const std::shared_ptr<Objects::Actor>& first = swapped ? selection[1] : selection[0];
        const std::shared_ptr<Objects::Actor>& second = swapped ? selection[0] : selection[1];
        const JPH::BodyID a = BodyOf(first);
        const JPH::BodyID b = BodyOf(second);
        if (a.IsInvalid() || b.IsInvalid())
        {
            ImGui::Spacing();
            ImGui::TextWrapped("Both actors need a physics body. %s has none.", a.IsInvalid() ? first->GetName().c_str() : second->GetName().c_str());
            return;
        }

        const Physics::Explanation why = system->Explain(a, b);

        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s  vs  %s", first->GetName().c_str(), second->GetName().c_str());
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
        if (ImGui::Button(ICON_LC_ARROW_LEFT_RIGHT, ImVec2(ImGui::GetFrameHeight(), 0.0f)))
            swapped = !swapped;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Swap first and second");
        ImGui::Spacing();

        const ImVec4 color = ResponseColor(why.response);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetFontSize() * 3.2f;
        const float width = ImGui::GetContentRegionAvail().x;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(start, ImVec2(start.x + width, start.y + height), ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 0.16f)), 6.0f);
        draw->AddRectFilled(start, ImVec2(start.x + 4.0f, start.y + height), ImGui::ColorConvertFloat4ToU32(color), 6.0f, ImDrawFlags_RoundCornersLeft);
        ImGui::SetCursorScreenPos(ImVec2(start.x + 16.0f, start.y + ImGui::GetFontSize() * 0.45f));
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::Text("%s  %s", ResponseIcon(why.response), Collision::ToString(why.response));
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(ImVec2(start.x + 16.0f, start.y + ImGui::GetFontSize() * 1.7f));
        ImGui::TextDisabled("%s", why.groupIgnored ? "This pair is ignored with IgnoreCollisionWith." : ResponseMeaning(why.response));
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + height + ImGui::GetStyle().ItemSpacing.y * 2.0f));

        if (ImGui::BeginTable("##ExplainSides", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawSideCard("FIRST", why.sides[0], why.reasons, "first: ");
            ImGui::TableSetColumnIndex(1);
            DrawSideCard("SECOND", why.sides[1], why.reasons, "second: ");
            ImGui::EndTable();
        }

        bool notes = false;
        for (const std::string& reason : why.reasons)
        {
            if (StartsWith(reason, "first: ") || StartsWith(reason, "second: ") || StartsWith(reason, "result :"))
                continue;
            if (!notes)
            {
                ImGui::SeparatorText("Notes");
                notes = true;
            }
            ImGui::BulletText("%s", reason.c_str());
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Full trace"))
        {
            if (ImGui::SmallButton(ICON_LC_COPY " Copy"))
                ImGui::SetClipboardText(why.Text().c_str());
            ImGui::TextWrapped("%s", why.Text().c_str());
        }
    }

    void CollisionDebugPanel::DrawRecorder()
    {
        Physics::CollisionSystem* system = System();
        Collision::Recorder& recorder = system->GetRecorder();

        if (ImGui::Checkbox("Pause recording", &paused))
            system->SetRecording(!paused);
        ImGui::SameLine();
        if (ImGui::Button(ICON_LC_TRASH_2 " Clear"))
            recorder.Clear();
        ImGui::SameLine();
        ImGui::Text("%zu records", recorder.Size());

        if (recorder.Size() == 0)
        {
            ImGui::TextDisabled("Nothing recorded yet.");
            return;
        }

        const int first = int(recorder.FirstStep());
        const int last = int(recorder.LastStep());
        if (follow || step < first || step > last)
            step = last;
        ImGui::Checkbox("Follow", &follow);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderInt("##Step", &step, first, last, "step %d"))
            follow = false;
        if (ImGui::Button(ICON_LC_CHEVRON_LEFT) && step > first) { --step; follow = false; }
        ImGui::SameLine();
        if (ImGui::Button(ICON_LC_CHEVRON_RIGHT) && step < last) { ++step; follow = false; }

        const std::vector<Collision::Record> records = recorder.Step(uint64_t(step));
        if (ImGui::BeginTable("##Records", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Kind");
            ImGui::TableSetupColumn("A");
            ImGui::TableSetupColumn("B");
            ImGui::TableSetupColumn("Value");
            ImGui::TableSetupColumn("Position");
            ImGui::TableHeadersRow();
            for (const Collision::Record& r : records)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s %u", kRecordKinds[std::min<int>(int(r.kind), 3)], unsigned(r.detail));
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%llu", (unsigned long long)r.a);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%llu", (unsigned long long)r.b);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.2f", r.value);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.2f %.2f %.2f", r.position[0], r.position[1], r.position[2]);
            }
            ImGui::EndTable();
        }
    }

    void CollisionDebugPanel::DrawLive()
    {
        Physics::CollisionSystem* system = System();
        Debug::CollisionDebugState& debug = Debug::CollisionDebugState::Get();
        ImGui::SeparatorText("Viewport");
        ImGui::Checkbox("Draw contacts and events", &debug.overlay);
        ImGui::BeginDisabled(!debug.overlay);
        ImGui::Checkbox("Contact points", &debug.contacts);
        ImGui::SameLine();
        ImGui::Checkbox("Normals", &debug.normals);
        ImGui::SameLine();
        ImGui::Checkbox("Event log", &debug.events);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        ImGui::SliderFloat("Fade", &debug.fadeSeconds, 0.2f, 10.0f, "%.1f s");
        if (ImGui::Button(ICON_LC_TRASH_2 " Clear marks"))
            system->ClearDebug();
        ImGui::EndDisabled();
        const bool highlighting = debug.highlight.lock() != nullptr;
        ImGui::BeginDisabled(!highlighting);
        if (ImGui::Button(ICON_LC_X " Stop highlighting"))
            debug.highlight.reset();
        ImGui::EndDisabled();

        ImGui::SeparatorText("State");
        ImGui::Text("Collision classes : %u / %u", system->Classes().AliveCount(), system->Classes().MaxClasses());
        ImGui::Text("Subscriptions : %zu", system->SubscriptionCount());
        ImGui::Text("Recording : %s", system->Recording() ? "on" : "off");

        ImGui::SeparatorText("Overlaps of the selection");
        const auto actor = parent ? parent->GetSelectedActor() : nullptr;
        const JPH::BodyID body = BodyOf(actor);
        if (body.IsInvalid())
        {
            ImGui::TextDisabled("Select an actor with a physics body.");
            return;
        }
        const std::vector<JPH::BodyID> overlaps = system->Overlapping(body);
        if (overlaps.empty())
            ImGui::TextDisabled("Nothing right now.");
        for (JPH::BodyID other : overlaps)
            ImGui::Text("body %u", unsigned(other.GetIndex()));
    }
}
