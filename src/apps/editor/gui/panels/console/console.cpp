#include "console.hpp"

#include "apps/editor/gui/main_window.hpp"

namespace Shard::Editor::GUI{

    void Console::SetParentWindow(Core::EditorMainWindow *parent)
    {
        this->parent = parent;

        auto AddLog = [this](const Engine::Debugging::Level& level, const std::string& message){
            logs.push_back({level, message});
        };

        Engine::Debugging::GetLogger().AddSink(AddLog);
    }

    void Console::Draw()
    {
        ImGui::Begin("Console");

        static bool showLog     = true;
        static bool showInfo    = true;
        static bool showWarning = true;
        static bool showError   = true;
        static bool showFatal   = true;

        // Filter buttons
        ImGui::Text("Filter :");
        ImGui::SameLine(); ImGui::Checkbox("Log", &showLog);
        ImGui::SameLine(); ImGui::Checkbox("Info", &showInfo);
        ImGui::SameLine(); ImGui::Checkbox("Warning", &showWarning);
        ImGui::SameLine(); ImGui::Checkbox("Error", &showError);
        ImGui::SameLine(); ImGui::Checkbox("Fatal", &showFatal);

        ImGui::Separator();

        // Scrollable log region
        ImGui::BeginChild("##ConsoleScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& [level, message] : logs)
        {
            bool show = false;
            ImVec4 color;

            switch (level)
            {
                case Engine::Debugging::Level::Log:
                    show = showLog;
                    color = ImVec4(1.f, 1.f, 1.f, 1.f);
                    break;

                case Engine::Debugging::Level::Info:
                    show = showInfo;
                    color = ImVec4(0.4f, 0.8f, 1.f, 1.f);
                    break;

                case Engine::Debugging::Level::Warning:
                    show = showWarning;
                    color = ImVec4(1.f, 0.8f, 0.2f, 1.f);
                    break;

                case Engine::Debugging::Level::Error:
                    show = showError;
                    color = ImVec4(1.f, 0.3f, 0.3f, 1.f);
                    break;

                case Engine::Debugging::Level::Fatal:
                    show = showFatal;
                    color = ImVec4(0.8f, 0.f, 0.f, 1.f);
                    break;
            }

            if (!show)
                continue;

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("%s", message.c_str());
            ImGui::PopStyleColor();
        }

        // Auto-scroll
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();

        ImGui::End();
    }
}