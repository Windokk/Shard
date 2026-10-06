#pragma once

#include <memory>
#include <string>

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Rendering{
    class Material;
}

namespace Shard::Engine::Serialization{

    std::shared_ptr<Rendering::Material> DeserializeMaterial(const Filesystem::Path path);

}