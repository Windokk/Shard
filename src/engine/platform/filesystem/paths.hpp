#pragma once

#include <string>

namespace Shard::Engine::Core::Platform::Paths {

    std::string HomeDirectory();
    std::string DocumentsDirectory();
    std::string TempDirectory();

    std::string UserConfigDirectory(const std::string& app);
    std::string UserDataDirectory(const std::string& app);
    std::string UserCacheDirectory(const std::string& app);

    std::string ExecutableDirectory();

    std::string Normalize(std::string path);

    std::string ToNative(std::string path);
}
