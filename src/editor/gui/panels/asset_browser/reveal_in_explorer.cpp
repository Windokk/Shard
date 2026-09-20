#include "asset_operations.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#else
#include <cstdlib>
#endif

#include <algorithm>
#include <filesystem>

namespace Shard::Editor::GUI::AssetOperations
{
    void RevealInFileExplorer(const std::string& nativePath, bool selectItem)
    {
#if defined(_WIN32)
        std::string path = nativePath;
        std::replace(path.begin(), path.end(), '/', '\\');

        std::string args = selectItem ? "/select,\"" + path + "\"" : "\"" + path + "\"";
        ShellExecuteA(nullptr, "open", "explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
#else
        std::filesystem::path p(nativePath);
        std::string dir = (selectItem ? p.parent_path() : p).string();
        std::string command = "xdg-open \"" + dir + "\" >/dev/null 2>&1 &";
        std::system(command.c_str());
#endif
    }
}
