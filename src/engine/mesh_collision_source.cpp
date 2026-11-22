#include "engine/mesh_collision_source.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/renderer/components/model_component.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace Shard::Engine::Physics
{
    namespace
    {
        bool NativePath(Core::IEngineContext& engine, Filesystem::AssetID id, std::string& out)
        {
            std::shared_ptr<Filesystem::AssetInfos> info = engine.GetAssetIDManager()->GetAssetFromID(id);
            if (!info)
                return false;
            out = info->baseInfos.path.GetNativePath();
            return !out.empty();
        }
    }

    bool DecodeMeshToSoup(const Filesystem::Path& file, TriangleSoup& soup)
    {
        const Rendering::MeshCPUData data = Rendering::DecodeMeshFile(file);
        if (!data.success)
            return false;

        const Rendering::VertexElement* position = nullptr;
        for (const Rendering::VertexElement& element : data.layout.GetElements())
            if (element.location == 0) { position = &element; break; }
        const uint32_t stride = data.layout.GetStride();
        if (!position || stride < position->offset + sizeof(float) * 3 || data.vertices.size() % stride != 0)
            return false;

        const size_t count = data.vertices.size() / stride;
        soup.positions.resize(count);
        for (size_t i = 0; i < count; ++i)
            std::memcpy(&soup.positions[i], data.vertices.data() + i * stride + position->offset, sizeof(float) * 3);

        soup.indices.reserve(data.indices.size());
        for (const Rendering::SubMesh& submesh : data.submeshes)
        {
            const size_t first = submesh.indexOffset;
            const size_t last = std::min(first + submesh.indexCount, data.indices.size());
            for (size_t i = first; i + 3 <= last; i += 3)
            {
                soup.indices.push_back(data.indices[i]);
                soup.indices.push_back(data.indices[i + 1]);
                soup.indices.push_back(data.indices[i + 2]);
                soup.triangleSubmesh.push_back(submesh.materialIndex);
            }
            soup.submeshCount = std::max(soup.submeshCount, submesh.materialIndex + 1);
        }
        return !soup.indices.empty();
    }

    uint64_t SourceStamp(const std::string& path)
    {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec) return 0;
        const auto time = std::filesystem::last_write_time(path, ec);
        if (ec) return 0;
        const uint64_t stamp = (uint64_t(size) * 1099511628211ull) ^ uint64_t(time.time_since_epoch().count());
        return stamp == 0 ? 1 : stamp;
    }

    void InstallMeshColliderSource(PhysicsManager& physics, Core::IEngineContext& engine)
    {
        MeshSource source;
        Core::IEngineContext* context = &engine;

        source.path = [context](Filesystem::AssetID id) {
            std::string path;
            NativePath(*context, id, path);
            return path;
        };

        source.stamp = [context](Filesystem::AssetID id) -> uint64_t {
            std::string path;
            return NativePath(*context, id, path) ? SourceStamp(path) : 0;
        };

        source.load = [context](Filesystem::AssetID id, TriangleSoup& soup) {
            std::shared_ptr<Filesystem::AssetInfos> info = context->GetAssetIDManager()->GetAssetFromID(id);
            return info && DecodeMeshToSoup(info->baseInfos.path, soup);
        };

        source.modelMesh = [](Objects::Actor& actor) -> Filesystem::AssetID {
            auto model = actor.GetComponent<Objects::Components::Model>();
            return model ? model->mesh : Filesystem::AssetID();
        };

        physics.MeshColliders().SetSource(std::move(source));
    }
}
