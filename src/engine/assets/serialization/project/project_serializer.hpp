#pragma once

#include "engine/assets/project/project.hpp"

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Serialization{

    std::shared_ptr<Projects::Project> DeserializeProject(const Filesystem::Path path);
    void SerializeProject(Projects::Project* pro, const Filesystem::Path path);
}