#include "engine/platform/filesystem/paths.hpp"

#include "engine/platform/process/process.hpp"

#include <algorithm>
#include <filesystem>

namespace Shard::Engine::Core::Platform::Paths {

    namespace {
        std::string Env(const char* name)
        {
            return GetEnv(name).value_or("");
        }

        std::string Join(const std::string& a, const std::string& b)
        {
            if (a.empty())
                return b;
            return a + "/" + b;
        }

        std::string Ensure(const std::string& path)
        {
            if (!path.empty())
            {
                std::error_code ec;
                std::filesystem::create_directories(std::filesystem::u8path(path), ec);
            }
            return path;
        }
    }

    std::string Normalize(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        while (path.size() > 1 && path.back() == '/')
            path.pop_back();
        return path;
    }

    std::string ToNative(std::string path)
    {
    #if defined(_WIN32)
        std::replace(path.begin(), path.end(), '/', '\\');
    #endif
        return path;
    }

    std::string HomeDirectory()
    {
    #if defined(_WIN32)
        return Normalize(Env("USERPROFILE"));
    #else
        return Normalize(Env("HOME"));
    #endif
    }

    std::string DocumentsDirectory()
    {
    #if defined(_WIN32)
        return Join(HomeDirectory(), "Documents");
    #else
        const std::string xdg = Env("XDG_DOCUMENTS_DIR");
        return xdg.empty() ? Join(HomeDirectory(), "Documents") : Normalize(xdg);
    #endif
    }

    std::string TempDirectory()
    {
        std::error_code ec;
        const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
        return ec ? std::string() : Normalize(temp.u8string());
    }

    std::string UserConfigDirectory(const std::string& app)
    {
    #if defined(_WIN32)
        return Ensure(Join(Normalize(Env("APPDATA")), app));
    #elif defined(__APPLE__)
        return Ensure(Join(Join(HomeDirectory(), "Library/Application Support"), app));
    #else
        const std::string xdg = Env("XDG_CONFIG_HOME");
        return Ensure(Join(xdg.empty() ? Join(HomeDirectory(), ".config") : Normalize(xdg), app));
    #endif
    }

    std::string UserDataDirectory(const std::string& app)
    {
    #if defined(_WIN32)
        return Ensure(Join(Normalize(Env("LOCALAPPDATA")), app));
    #elif defined(__APPLE__)
        return Ensure(Join(Join(HomeDirectory(), "Library/Application Support"), app));
    #else
        const std::string xdg = Env("XDG_DATA_HOME");
        return Ensure(Join(xdg.empty() ? Join(HomeDirectory(), ".local/share") : Normalize(xdg), app));
    #endif
    }

    std::string UserCacheDirectory(const std::string& app)
    {
    #if defined(_WIN32)
        return Ensure(Join(UserDataDirectory(app), "cache"));
    #elif defined(__APPLE__)
        return Ensure(Join(Join(HomeDirectory(), "Library/Caches"), app));
    #else
        const std::string xdg = Env("XDG_CACHE_HOME");
        return Ensure(Join(xdg.empty() ? Join(HomeDirectory(), ".cache") : Normalize(xdg), app));
    #endif
    }

    std::string ExecutableDirectory()
    {
        return Platform::ExecutableDirectory();
    }
}
