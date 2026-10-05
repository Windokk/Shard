#pragma once

#include <string>
#include <vector>
#include <functional>

// Blocking, center-screen modal popups (Error/Warning/Info), styled to match the editor's own
// "card" UI language (see DrawLoadingOverlay in main_window.cpp and the progress-toast card in
// ImGuiNotify.hpp). Unlike editor/gui/notifications.hpp's corner toasts (fire-and-forget, auto
// dismiss), these dim and block the rest of the editor until the user picks a button - use them
// when the user has to make a decision before an action can proceed (e.g. unsaved changes).
//
// All functions must be called from the UI thread, between ImGui::NewFrame() and ImGui::Render().
// Draw() has to be called exactly once per frame, after all other ImGui windows, for anything to
// show up.
namespace Shard::Editor::GUI::Popups
{
    enum class PopupType
    {
        Info,
        Warning,
        Error
    };

    struct PopupButton
    {
        std::string label;
        std::function<void()> onClick; // may be null (e.g. a plain "Cancel"/"OK")
    };

    // Queues a popup. Popups are shown one at a time, in the order they were queued, each blocking
    // the rest of the editor until dismissed. If `buttons` is empty, a single "OK" button is added
    // automatically.
    void Show(PopupType type, const std::string& title, const std::string& message, std::vector<PopupButton> buttons = {});

    // Convenience for guarding an unload/close of a dirty document (a scene or a material): shows a
    // Warning popup offering Save / Discard / Cancel. Save runs `onSave`, Discard runs `onDiscard`;
    // Cancel (and closing the popup any other way, e.g. Escape) runs neither, so the caller's
    // pending unload/close should only happen from inside one of those two callbacks.
    void ConfirmUnsavedChanges(const std::string& itemName, const std::function<void()>& onSave, const std::function<void()>& onDiscard);

    // Draws the currently active popup, if any. Call once per frame, after all other ImGui windows.
    void Draw();
}
