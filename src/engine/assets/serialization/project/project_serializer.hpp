#pragma once

#include "engine/assets/project/project.hpp"

#include "engine/assets/vfs/filesystem.hpp"

#include <nlohmann/json.hpp>

namespace Shard::Engine::Serialization{

    std::shared_ptr<Projects::Project> DeserializeProject(const Filesystem::Path path);
    void SerializeProject(Projects::Project* pro, const Filesystem::Path path);
    nlohmann::ordered_json ProjectToJson(Projects::Project* pro);
}