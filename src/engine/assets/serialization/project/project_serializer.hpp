#pragma once

#include "engine/projects/project.hpp"

#include "engine/filesystem/filesystem.hpp"

namespace Shard::Engine::Serialization{

    std::shared_ptr<Projects::Project> DeserializeProject(const Filesystem::Path path);
    void SerializeProject(Projects::Project* pro, const Filesystem::Path path);
}