#pragma once

#include <string>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Rendering {

    std::string ResolveGLSLIncludes(const Filesystem::Path& path);

}
