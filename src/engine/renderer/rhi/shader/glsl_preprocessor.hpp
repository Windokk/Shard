#pragma once

#include <string>
#include <vector>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Rendering {

    std::string ResolveGLSLIncludes(const Filesystem::Path& path);

    std::string InjectGLSLDefines(const std::string& source, const std::vector<std::string>& defines);

}
