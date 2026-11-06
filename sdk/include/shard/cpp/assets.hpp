// Shard C++ SDK : the assets of the project, by path and by id (shard_assets.h).
//
//   if (auto id = Shard::Assets::Resolve("models/level.fbx")) {
//       if (auto mesh = Shard::Assets::ReadMesh(*id)) Bake(mesh->positions, mesh->indices);       // a navigation mesh from the level's triangles
//   }
//   Shard::Assets::Write("navigation/level.nav", bytes.data(), bytes.size());                      // a file of the project, indexed as an asset
//
// A path is a path in the project ("models/crate.fbx") ; a mod:// address is accepted wherever a file is read. thread : main unless said otherwise.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <shard/shard_assets.h>
#include <shard/cpp/component.hpp>
#include <shard/cpp/detail.hpp>

namespace Shard {

    enum class AssetKind : uint32_t {
        Unknown = SHARD_ASSET_UNKNOWN, Mesh = SHARD_ASSET_MESH, Texture = SHARD_ASSET_TEXTURE, Material = SHARD_ASSET_MATERIAL, Shader = SHARD_ASSET_SHADER,
        World = SHARD_ASSET_WORLD, Sound = SHARD_ASSET_SOUND, Prefab = SHARD_ASSET_PREFAB, Plugin = SHARD_ASSET_PLUGIN,
    };

    struct AssetInfo {
        int32_t id = 0;
        AssetKind kind = AssetKind::Unknown;
        std::string path;
        std::string typeName;           ///< a plugin's asset type : its name
        uint64_t sizeBytes = 0;
        int64_t modifiedTime = 0;
    };

    /// The triangles of a mesh asset, in the mesh's own space
    struct MeshData {
        std::vector<float> positions;   ///< 3 floats a vertex
        std::vector<float> normals;
        std::vector<float> uvs;         ///< 2 floats a vertex
        std::vector<uint32_t> indices;  ///< triangle list
        struct Slice { uint32_t indexOffset = 0, indexCount = 0, materialSlot = 0; };
        std::vector<Slice> slices;
        Vec3 boundsMin, boundsMax;
        uint32_t VertexCount() const { return uint32_t(positions.size() / 3); }
    };

    /// An image decoded to 8 bit RGBA, rows from the top
    struct ImageData {
        uint32_t width = 0, height = 0;
        std::vector<uint8_t> rgba;
    };

    class Assets {
    public:
        /// The id of the asset at that path, if the project has one. thread : any
        static std::optional<int32_t> Resolve(std::string_view path)
        {
            int32_t id = 0;
            if (Api().resolve(Str(path), &id) != SHARD_OK) return std::nullopt;
            return id;
        }
        /// thread : any
        static std::string PathOf(int32_t id)
        {
            std::string out;
            ReadText([&](char* b, uint64_t c, uint64_t* s) { return Api().path_of(id, b, c, s); }, out);
            return out;
        }
        /// thread : any
        static std::optional<AssetInfo> Info(int32_t id)
        {
            ShardAssetInfo raw{};
            raw.structSize = sizeof(ShardAssetInfo);
            if (Api().info(id, &raw) != SHARD_OK) return std::nullopt;
            return Convert(raw);
        }
        /// The assets under `subdir` of the project ("" : all) of that kind, in the order of their paths
        static std::vector<AssetInfo> List(std::string_view subdir = {}, AssetKind kind = AssetKind::Unknown)
        {
            std::vector<AssetInfo> out;
            Api().list(Str(subdir), uint32_t(kind), [](void* user, const ShardAssetInfo* a) -> ShardResult {
                static_cast<std::vector<AssetInfo>*>(user)->push_back(Convert(*a));
                return SHARD_OK;
            }, &out);
            return out;
        }
        static bool Exists(std::string_view path) { return Api().exists(Str(path)) == SHARD_TRUE; }

        /// A whole file. Empty optional if there is none. thread : any
        static std::optional<std::vector<uint8_t>> Read(std::string_view path)
        {
            void* data = nullptr;
            uint64_t size = 0;
            if (Api().read(detail::PluginHandle(), Str(path), &data, &size) != SHARD_OK) return std::nullopt;
            std::vector<uint8_t> out(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
            detail::Host()->free(detail::PluginHandle(), data);
            return out;
        }
        static std::optional<std::string> ReadTextFile(std::string_view path)
        {
            auto bytes = Read(path);
            if (!bytes) return std::nullopt;
            return std::string(bytes->begin(), bytes->end());
        }
        /// Writes a file of the project (its folders made, replaced if it exists) and has the engine index it : its asset id.
        static std::optional<int32_t> Write(std::string_view path, const void* data, uint64_t size)
        {
            int32_t id = 0;
            if (Api().write(detail::PluginHandle(), Str(path), data, size, &id) != SHARD_OK) return std::nullopt;
            return id;
        }

        /// The geometry of a model asset
        static std::optional<MeshData> ReadMesh(int32_t id)
        {
            ShardMeshRead io{};
            io.structSize = sizeof(ShardMeshRead);
            if (Api().mesh_read(id, &io) != SHARD_OK) return std::nullopt;       // the counts, with capacities of 0
            MeshData out;
            out.positions.resize(size_t(io.vertexCount) * 3);
            out.normals.resize(size_t(io.vertexCount) * 3);
            out.uvs.resize(size_t(io.vertexCount) * 2);
            out.indices.resize(io.indexCount);
            std::vector<ShardMeshSlice> slices(io.sliceCount);
            ShardMeshRead fill{};
            fill.structSize = sizeof(ShardMeshRead);
            fill.vertexCapacity = io.vertexCount; fill.indexCapacity = io.indexCount; fill.sliceCapacity = io.sliceCount;
            fill.positions = out.positions.data(); fill.normals = out.normals.data(); fill.uvs = out.uvs.data();
            fill.indices = out.indices.data(); fill.slices = slices.data();
            if (Api().mesh_read(id, &fill) != SHARD_OK) return std::nullopt;
            for (const ShardMeshSlice& s : slices) out.slices.push_back({ s.indexOffset, s.indexCount, s.materialSlot });
            ShardMeshInfo info{};
            info.structSize = sizeof(ShardMeshInfo);
            if (Api().mesh_info(id, &info) == SHARD_OK) {
                out.boundsMin = Vec3{ info.boundsMin[0], info.boundsMin[1], info.boundsMin[2] };
                out.boundsMax = Vec3{ info.boundsMax[0], info.boundsMax[1], info.boundsMax[2] };
            }
            return out;
        }
        /// An image asset decoded. thread : any
        static std::optional<ImageData> ReadImage(int32_t id)
        {
            ImageData out;
            if (Api().image_info(id, &out.width, &out.height) != SHARD_OK) return std::nullopt;
            uint64_t size = 0;
            out.rgba.resize(size_t(out.width) * out.height * 4);
            if (Api().image_read(id, out.rgba.data(), out.rgba.size(), &size) != SHARD_OK) return std::nullopt;
            return out;
        }

    private:
        static AssetInfo Convert(const ShardAssetInfo& a)
        {
            AssetInfo out;
            out.id = a.id;
            out.kind = AssetKind(a.kind);
            out.path = ToString(a.path);
            out.typeName = ToString(a.typeName);
            out.sizeBytes = a.sizeBytes;
            out.modifiedTime = a.modifiedTime;
            return out;
        }
        static const ShardAssetsApi& Api() { return detail::Require<ShardAssetsApi>(SHARD_IFACE_ASSETS); }
    };
}
