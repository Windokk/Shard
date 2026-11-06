#pragma once

#include <string>
#include <vector>
#include <functional>

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
        std::function<void()> onClick;
    };

    void Show(PopupType type, const std::string& title, const std::string& message, std::vector<PopupButton> buttons = {});

    // Cancel or Escape run neither callback, so do the unload only inside onSave/onDiscard.
    void ConfirmUnsavedChanges(const std::string& itemName, const std::function<void()>& onSave, const std::function<void()>& onDiscard);

    void Draw();
}
