#include "engine/platform/clipboard/clipboard.hpp"

#include <SDL3/SDL.h>

#include <memory>

namespace Shard::Engine::Core::Platform {

    namespace Clipboard {

        void SetText(const std::string& text)
        {
            SDL_SetClipboardText(text.c_str());
        }

        std::string GetText()
        {
            char* text = SDL_GetClipboardText();
            if (!text)
                return "";
            std::string ret = text;
            SDL_free(text);
            return ret;
        }

        bool HasText()
        {
            return SDL_HasClipboardText();
        }
    }

    namespace Dialogs {

        namespace {
            struct DialogRequest {
                DialogCallback callback;
                std::vector<std::string> filterNames;
                std::vector<std::string> filterPatterns;
                std::vector<SDL_DialogFileFilter> filters;
            };

            void SDLCALL OnDialogDone(void* userdata, const char* const* files, int)
            {
                std::unique_ptr<DialogRequest> request(static_cast<DialogRequest*>(userdata));

                std::vector<std::string> paths;
                // files == NULL : error, *files == NULL : cancelled
                for (int i = 0; files && files[i]; ++i)
                    paths.push_back(files[i]);

                if (request->callback)
                    request->callback(paths);
            }

            std::unique_ptr<DialogRequest> MakeRequest(const FileDialogDesc& desc, DialogCallback callback)
            {
                auto request = std::make_unique<DialogRequest>();
                request->callback = std::move(callback);

                // SDL keeps pointers to the strings : they live as long as the request.
                request->filterNames.reserve(desc.filters.size());
                request->filterPatterns.reserve(desc.filters.size());
                for (const FileFilter& filter : desc.filters)
                {
                    request->filterNames.push_back(filter.name);
                    request->filterPatterns.push_back(filter.pattern);
                    request->filters.push_back({ request->filterNames.back().c_str(), request->filterPatterns.back().c_str() });
                }
                return request;
            }
        }

        void ShowOpenFileDialog(const FileDialogDesc& desc, DialogCallback callback)
        {
            DialogRequest* request = MakeRequest(desc, std::move(callback)).release();
            SDL_ShowOpenFileDialog(OnDialogDone, request, static_cast<SDL_Window*>(desc.parentWindow),
                request->filters.empty() ? nullptr : request->filters.data(), static_cast<int>(request->filters.size()),
                desc.defaultPath.empty() ? nullptr : desc.defaultPath.c_str(), desc.allowMultiple);
        }

        void ShowSaveFileDialog(const FileDialogDesc& desc, DialogCallback callback)
        {
            DialogRequest* request = MakeRequest(desc, std::move(callback)).release();
            SDL_ShowSaveFileDialog(OnDialogDone, request, static_cast<SDL_Window*>(desc.parentWindow),
                request->filters.empty() ? nullptr : request->filters.data(), static_cast<int>(request->filters.size()),
                desc.defaultPath.empty() ? nullptr : desc.defaultPath.c_str());
        }

        void ShowOpenFolderDialog(const FileDialogDesc& desc, DialogCallback callback)
        {
            DialogRequest* request = MakeRequest(desc, std::move(callback)).release();
            SDL_ShowOpenFolderDialog(OnDialogDone, request, static_cast<SDL_Window*>(desc.parentWindow),
                desc.defaultPath.empty() ? nullptr : desc.defaultPath.c_str(), desc.allowMultiple);
        }

        void ShowMessageBox(MessageKind kind, const std::string& title, const std::string& message, void* parentWindow)
        {
            SDL_MessageBoxFlags flags = SDL_MESSAGEBOX_INFORMATION;
            if (kind == MessageKind::Warning) flags = SDL_MESSAGEBOX_WARNING;
            else if (kind == MessageKind::Error) flags = SDL_MESSAGEBOX_ERROR;

            SDL_ShowSimpleMessageBox(flags, title.c_str(), message.c_str(), static_cast<SDL_Window*>(parentWindow));
        }
    }
}
