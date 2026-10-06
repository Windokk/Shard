#pragma once

#include <string>

namespace Shard::Engine::Core::Platform::Paths {

    // All the returned paths are UTF-8 with forward slashes, without a trailing slash.
    // The `app` directories are created when they don't exist.

    std::string HomeDirectory();
    std::string DocumentsDirectory();
    std::string TempDirectory();

    /// @brief Per-user settings : %APPDATA%/<app> , $XDG_CONFIG_HOME/<app> , ~/Library/Application Support/<app>
    std::string UserConfigDirectory(const std::string& app);
    /// @brief Per-user data that is not settings (saves, downloaded content) : %LOCALAPPDATA%/<app> , $XDG_DATA_HOME/<app>
    std::string UserDataDirectory(const std::string& app);
    /// @brief Disposable per-user files (shader caches, thumbnails) : %LOCALAPPDATA%/<app>/cache , $XDG_CACHE_HOME/<app>
    std::string UserCacheDirectory(const std::string& app);

    /// @brief Directory of the running executable.
    std::string ExecutableDirectory();

    /// @brief "\" -> "/" (what the engine stores and compares).
    std::string Normalize(std::string path);

    /// @brief "/" -> the separator of the OS (what the OS APIs and the user expect to see).
    std::string ToNative(std::string path);
}
