#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Shard::Engine::Core::Platform {

    namespace Clipboard {
        void SetText(const std::string& text);
        std::string GetText();
        bool HasText();
    }

    // File dialogs are async. The callback gets chosen paths (empty if cancelled) and may run on another thread.
    namespace Dialogs {
        struct FileFilter {
            std::string name;
            std::string pattern;
        };

        struct FileDialogDesc {
            std::string title;
            std::string defaultPath;
            std::vector<FileFilter> filters;
            bool allowMultiple = false;
            void* parentWindow = nullptr;
        };

        using DialogCallback = std::function<void(const std::vector<std::string>& paths)>;

        void ShowOpenFileDialog(const FileDialogDesc& desc, DialogCallback callback);
        void ShowSaveFileDialog(const FileDialogDesc& desc, DialogCallback callback);
        void ShowOpenFolderDialog(const FileDialogDesc& desc, DialogCallback callback);

        enum class MessageKind { Info, Warning, Error };
        void ShowMessageBox(MessageKind kind, const std::string& title, const std::string& message, void* parentWindow = nullptr);
    }
}
