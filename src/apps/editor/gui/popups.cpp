#include "apps/editor/gui/popups.hpp"

#include "imgui/imgui.h"

#include "apps/editor/gui/IconsLucide.h"

#include <cstdint>
#include <deque>

namespace Shard::Editor::GUI::Popups
{
    namespace
    {
        struct PendingPopup
        {
            uint64_t id = 0;
            PopupType type = PopupType::Info;
            std::string title;
            std::string message;
            std::vector<PopupButton> buttons;
        };

        std::deque<PendingPopup> g_queue;
        uint64_t g_nextId = 1;

        // Which queued popup ImGui currently has open, and whether BeginPopupModal has actually
        // returned true for it at least once - lets Draw() tell "not opened yet" apart from
        // "was open, got closed some other way than one of our own buttons" (e.g. Escape).
        uint64_t g_activeId = 0;
        bool g_activeOpenedOnce = false;

        const char* GetIcon(PopupType type)
        {
            switch (type)
            {
                case PopupType::Error:   return ICON_LC_CIRCLE_ALERT;
                case PopupType::Warning: return ICON_LC_TRIANGLE_ALERT;
                case PopupType::Info:    return ICON_LC_INFO;
            }
            return ICON_LC_INFO;
        }

        ImU32 GetAccentColor(PopupType type)
        {
            switch (type)
            {
                case PopupType::Error:   return IM_COL32(214, 100, 100, 255);
                case PopupType::Warning: return IM_COL32(224, 178, 90, 255);
                // Matches the editor's own blue-teal accent (see ImGuiCol_SliderGrabActive /
                // ButtonHovered in SetupImGuiStyle, and DrawLoadingOverlay's accent).
                case PopupType::Info:    return IM_COL32(130, 178, 212, 255);
            }
            return IM_COL32(255, 255, 255, 255);
        }
    }

    void Show(PopupType type, const std::string& title, const std::string& message, std::vector<PopupButton> buttons)
    {
        PendingPopup popup;
        popup.id = g_nextId++;
        popup.type = type;
        popup.title = title;
        popup.message = message;
        popup.buttons = std::move(buttons);

        if (popup.buttons.empty())
            popup.buttons.push_back(PopupButton{"OK", nullptr});

        g_queue.push_back(std::move(popup));
    }

    void ConfirmUnsavedChanges(const std::string& itemName, const std::function<void()>& onSave, const std::function<void()>& onDiscard)
    {
        Show(PopupType::Warning, "Unsaved Changes",
            itemName + " has unsaved changes. Save them before continuing?",
            {
                PopupButton{"Save", onSave},
                PopupButton{"Discard", onDiscard},
                PopupButton{"Cancel", nullptr}
            });
    }

    void Draw()
    {
        if (g_queue.empty())
            return;

        PendingPopup& popup = g_queue.front();

        constexpr const char* kPopupId = "##ShardPopupModal";

        if (g_activeId != popup.id)
        {
            g_activeId = popup.id;
            g_activeOpenedOnce = false;
            ImGui::OpenPopup(kPopupId);
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                        viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                                 ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Always);

        // Same opaque "card" treatment used by the loading overlay and the progress-toast (see
        // DrawLoadingOverlay in main_window.cpp / the hasProgressBar branch in ImGuiNotify.hpp) so
        // this reads as part of the editor rather than a different app's dialog box.
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.10f, 0.11f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.09f, 0.10f, 0.11f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.34f, 0.38f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

        const bool isOpen = ImGui::BeginPopupModal(kPopupId, nullptr, flags);
        if (isOpen)
        {
            g_activeOpenedOnce = true;

            const ImU32 accent = GetAccentColor(popup.type);
            const ImVec4 accentVec4 = ImGui::ColorConvertU32ToFloat4(accent);

            ImGui::TextColored(accentVec4, "%s", GetIcon(popup.type));
            ImGui::SameLine();
            ImGui::TextColored(accentVec4, "%s", popup.title.c_str());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
            ImGui::TextUnformatted(popup.message.c_str());
            ImGui::PopTextWrapPos();

            ImGui::Spacing();
            ImGui::Spacing();

            // Right-align the button row.
            constexpr float buttonWidth = 100.0f;
            const size_t buttonCount = popup.buttons.size();
            const float totalWidth = buttonCount * buttonWidth + (float)(buttonCount - 1) * ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - totalWidth);

            int clickedIndex = -1;
            for (int i = 0; i < (int)buttonCount; ++i)
            {
                ImGui::PushID(i);
                if (i > 0)
                    ImGui::SameLine();
                if (ImGui::Button(popup.buttons[i].label.c_str(), ImVec2(buttonWidth, 0.0f)))
                    clickedIndex = i;
                ImGui::PopID();
            }

            if (clickedIndex >= 0)
            {
                // Copy out before popping - `popup` (and popup.buttons[clickedIndex]) is a reference
                // into g_queue.front(), which pop_front() below invalidates.
                std::function<void()> onClick = popup.buttons[clickedIndex].onClick;

                ImGui::CloseCurrentPopup();
                g_queue.pop_front();
                g_activeId = 0;
                g_activeOpenedOnce = false;

                if (onClick)
                    onClick();
            }

            ImGui::EndPopup();
        }
        else if (g_activeOpenedOnce)
        {
            // Was open, closed some other way than one of our buttons (e.g. Escape) - treat like
            // Cancel: drop it with no callback so the next queued popup (if any) can take over.
            g_queue.pop_front();
            g_activeId = 0;
            g_activeOpenedOnce = false;
        }

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(3);
    }
}
