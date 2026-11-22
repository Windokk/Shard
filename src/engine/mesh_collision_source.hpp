#pragma once

#include <cstdint>
#include <string>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/physics/mesh/mesh_collider.hpp"

namespace Shard::Engine::Core { class IEngineContext; }

namespace Shard::Engine::Physics
{
    class PhysicsManager;

    bool DecodeMeshToSoup(const Filesystem::Path& file, TriangleSoup& soup);
    uint64_t SourceStamp(const std::string& nativePath);
    void InstallMeshColliderSource(PhysicsManager& physics, Core::IEngineContext& engine);
}
