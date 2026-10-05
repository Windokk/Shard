#pragma once

#include <string>
#include <vector>

#include "engine/filesystem/filesystem.hpp"

// File operations behind the asset browser's context menu. Unlike a plain filesystem call, each one keeps
// the rest of the project consistent : the asset database (IDs survive a rename/move, so the dependency
// lists that reference them stay valid), the level/material files that reference the asset by its path in
// the project, the build settings, and the resources manager's caches.
//
// Anything a loaded level (or a resident material) still depends on is refused - it cannot be renamed,
// moved or deleted from under its users - and reported through Result::message.
namespace Shard::Editor::GUI::AssetOperations
{
    struct Result
    {
        bool ok = true;
        std::string message;
    };

    /// @brief True if `path` is inside the project's resources (and is not the resources root itself) -
    /// the only place these operations work.
    bool IsEditable(const Engine::Filesystem::Path& path);

    /// @brief A path in `dir` that does not exist yet : `stem + extension`, then `stem_1`, `stem_2`... With
    /// `copySuffix` the first candidate is `stem_copy` (then `stem_copy2`...).
    Engine::Filesystem::Path UniquePath(const Engine::Filesystem::Path& dir, const std::string& stem, const std::string& extension, bool copySuffix = false);

    /// @brief Renames/moves a file or folder to `to` (a full path, in the project resources, that must not exist).
    Result Move(const Engine::Filesystem::Path& from, const Engine::Filesystem::Path& to);

    /// @brief Copies a file or folder to `to` (a full path that must not exist) and registers the copies as new assets.
    Result Copy(const Engine::Filesystem::Path& from, const Engine::Filesystem::Path& to);

    /// @brief Deletes a file or folder and unregisters its assets.
    Result Delete(const Engine::Filesystem::Path& path);

    /// @brief Names (in the project) of the other assets whose dependency list contains `path`, or something
    /// inside it if it is a folder. Based on the asset database, so it only knows about saved dependencies.
    std::vector<std::string> ReferencedBy(const Engine::Filesystem::Path& path);

    // Creation : each returns the new item's path in `created` (empty on failure).
    Result CreateFolder(const Engine::Filesystem::Path& dir, Engine::Filesystem::Path& created);
    Result CreateMaterial(const Engine::Filesystem::Path& dir, Engine::Filesystem::Path& created);
    Result CreateLevel(const Engine::Filesystem::Path& dir, Engine::Filesystem::Path& created);

    /// @brief Brings the asset database in line with the disk for everything under `dir` : files added
    /// outside the editor get an ID, entries whose file is gone (and unused) are dropped.
    /// @return A one-line summary of what changed (empty if nothing did)
    std::string Sync(const Engine::Filesystem::Path& dir);

    /// @brief Opens the OS file explorer on `path` (selecting it if it is a file).
    void RevealInFileExplorer(const std::string& nativePath, bool selectItem);
}
