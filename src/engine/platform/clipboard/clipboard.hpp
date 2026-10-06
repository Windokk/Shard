#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Shard::Engine::Core::Platform {

    /// @brief The OS clipboard (text). Needs the window system to be up (a window was created).
    namespace Clipboard {
        void SetText(const std::string& text);
        std::string GetText();
        bool HasText();
    }

    /// @brief Native dialogs. The file dialogs are asynchronous : the callback receives the chosen paths
    /// (empty if the user cancelled) and may run on another thread than the one that opened the dialog.
    namespace Dialogs {
        struct FileFilter {
            std::string name;       // "Images"
            std::string pattern;    // "png;jpg;jpeg" (extensions without the dot, separated by ';')
        };

        struct FileDialogDesc {
            std::string title;
            std::string defaultPath;
            std::vector<FileFilter> filters;
            bool allowMultiple = false;
            void* parentWindow = nullptr;   // IWindow::GetNativeHandle(), optional
        };

        using DialogCallback = std::function<void(const std::vector<std::string>& paths)>;

        void ShowOpenFileDialog(const FileDialogDesc& desc, DialogCallback callback);
        void ShowSaveFileDialog(const FileDialogDesc& desc, DialogCallback callback);
        void ShowOpenFolderDialog(const FileDialogDesc& desc, DialogCallback callback);

        enum class MessageKind { Info, Warning, Error };
        void ShowMessageBox(MessageKind kind, const std::string& title, const std::string& message, void* parentWindow = nullptr);
    }
}
