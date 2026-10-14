#pragma once

#include <string>
#include <vector>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Rendering {

    std::string ResolveGLSLIncludes(const Filesystem::Path& path);

    /// Puts `#define NAME [value]` for each of `defines` (`NAME` or `NAME=value`) right after the #version line of a stage
    std::string InjectGLSLDefines(const std::string& source, const std::vector<std::string>& defines);

}
