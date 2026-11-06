#pragma once

#include <string>
#include <vector>

#include "engine/assets/vfs/filesystem.hpp"

// Each operation keeps the asset database, the world and material files that reference the asset by path, the build settings and the resource caches consistent. Anything a loaded world or resident material uses is refused.
namespace Shard::Editor::GUI::AssetOperations
{
    struct Result
    {
        bool ok = true;
        std::string message;
    };

    bool IsEditable(const Engine::Filesystem::Path& path);

    Engine::Filesystem::Path UniquePath(const Engine::Filesystem::Path& dir, const std::string& stem, const std::string& extension, bool copySuffix = false);

    Result Move(const Engine::Filesystem::Path& from, const Engine::Filesystem::Path& to);

    Result Copy(const Engine::Filesystem::Path& from, const Engine::Filesystem::Path& to);

    Result Delete(const Engine::Filesystem::Path& path);

    std::vector<std::string> ReferencedBy(const Engine::Filesystem::Path& path);

    Result CreateFolder(const Engine::Filesystem::Path& dir, Engine::Filesystem::Path& created);
    Result CreateMaterial(const Engine::Filesystem::Path& dir, Engine::Filesystem::Path& created);
    Result CreateFromBytes(const Engine::Filesystem::Path& dir, const std::string& defaultName, const std::string& bytes, Engine::Filesystem::Path& created);
    Result CreateWorld(const Engine::Filesystem::Path& dir, Engine::Filesystem::Path& created);

    struct ImportSummary
    {
        int imported = 0;
        std::vector<std::string> failures;
    };

    ImportSummary Import(const Engine::Filesystem::Path& dir, const std::vector<std::string>& sources);

    std::string Sync(const Engine::Filesystem::Path& dir);

    void RevealInFileExplorer(const std::string& nativePath, bool selectItem);
}
