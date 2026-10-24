#include <algorithm>
#include <cstring>

#include <shard/shard_assets.h>

#include "engine/assets/assetID.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/assets/vfs/filesystem.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/world/engine.hpp"

// shard.assets, the part that needs the renderer's types : the geometry of a mesh asset and the pixels of an image asset. The rest is in
// src/engine/plugins/abi/c_assets.cpp ; c_internal.hpp declares these.

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        std::shared_ptr<Filesystem::AssetInfos> InfosOf(int32_t id)
        {
            Core::IEngineContext* engine = Core::gSharedEnginePtr;
            Filesystem::AssetIDManager* assets = engine ? engine->GetAssetIDManager() : nullptr;
            return assets ? assets->GetAssetFromID(Filesystem::AssetID(id)) : nullptr;
        }

        bool FindOffset(const Rendering::VertexLayout& layout, const char* name, uint32_t& offset)
        {
            for (const Rendering::VertexElement& element : layout.GetElements())
                if (element.name == name) { offset = element.offset; return true; }
            return false;
        }

        // The mesh asset and its geometry on the CPU side
        ShardResult MeshOf(int32_t id, std::shared_ptr<Rendering::Mesh>& mesh, std::shared_ptr<const Rendering::MeshCPUCopy>& cpu)
        {
            auto infos = InfosOf(id);
            if (!infos || infos->baseInfos.type != Filesystem::Type::T_MODEL) return SHARD_E_NOT_FOUND;
            Core::IEngineContext* engine = Core::gSharedEnginePtr;
            Core::Resources::ResourcesManager* resources = engine ? engine->GetResourcesManager() : nullptr;
            if (!resources) return SHARD_E_UNSUPPORTED;
            mesh = resources->Get<Rendering::Mesh>(Core::Resources::AssetKind::Mesh, infos->baseInfos.nameInProject);
            if (!mesh) return SHARD_E_NOT_FOUND;
            cpu = mesh->AcquireCPUData();
            return cpu ? SHARD_OK : SHARD_E_NOT_FOUND;
        }
    }

    ShardResult AssetMeshInfo(int32_t id, ShardMeshInfo* out)
    {
        std::shared_ptr<Rendering::Mesh> mesh;
        std::shared_ptr<const Rendering::MeshCPUCopy> cpu;
        const ShardResult r = MeshOf(id, mesh, cpu);
        if (r != SHARD_OK) return r;
        ShardMeshInfo info{};
        info.structSize = sizeof(ShardMeshInfo);
        info.vertexCount = uint32_t(mesh->GetVertexCount());
        info.indexCount = uint32_t(cpu->indices.size());
        info.submeshCount = uint32_t(mesh->GetSubMeshes().size());
        const glm::vec3 lo = mesh->GetBoundsMin(), hi = mesh->GetBoundsMax();
        info.boundsMin[0] = lo.x; info.boundsMin[1] = lo.y; info.boundsMin[2] = lo.z;
        info.boundsMax[0] = hi.x; info.boundsMax[1] = hi.y; info.boundsMax[2] = hi.z;
        const uint32_t asked = out->structSize;
        std::memcpy(out, &info, std::min<size_t>(asked, sizeof(ShardMeshInfo)));
        out->structSize = asked;
        return SHARD_OK;
    }

    ShardResult AssetMeshRead(int32_t id, ShardMeshRead* io)
    {
        std::shared_ptr<Rendering::Mesh> mesh;
        std::shared_ptr<const Rendering::MeshCPUCopy> cpu;
        const ShardResult r = MeshOf(id, mesh, cpu);
        if (r != SHARD_OK) return r;
        const Rendering::VertexLayout& layout = mesh->GetVertexLayout();
        const uint32_t stride = layout.GetStride();
        uint32_t posOffset = 0, normalOffset = 0, uvOffset = 0;
        const bool hasPos = FindOffset(layout, "aPos", posOffset);
        const bool hasNormal = FindOffset(layout, "aNormal", normalOffset);
        const bool hasUv = FindOffset(layout, "aTexCoord", uvOffset);
        if (!hasPos || stride == 0) return SHARD_E_NOT_FOUND;

        const uint32_t vertexCount = uint32_t(cpu->vertices.size() / stride);
        const uint32_t indexCount = uint32_t(cpu->indices.size());
        const auto& submeshes = mesh->GetSubMeshes();
        io->vertexCount = vertexCount;
        io->indexCount = indexCount;
        io->sliceCount = uint32_t(submeshes.size());

        const bool full = io->structSize >= sizeof(ShardMeshRead);
        if (!full) return SHARD_OK;
        const uint8_t* bytes = cpu->vertices.data();
        if (io->positions && io->vertexCapacity)
            for (uint32_t i = 0; i < std::min(vertexCount, io->vertexCapacity); ++i)
                std::memcpy(io->positions + i * 3, bytes + size_t(i) * stride + posOffset, sizeof(float) * 3);
        if (io->normals && io->vertexCapacity)
            for (uint32_t i = 0; i < std::min(vertexCount, io->vertexCapacity); ++i) {
                if (hasNormal) std::memcpy(io->normals + i * 3, bytes + size_t(i) * stride + normalOffset, sizeof(float) * 3);
                else { io->normals[i * 3] = 0.0f; io->normals[i * 3 + 1] = 1.0f; io->normals[i * 3 + 2] = 0.0f; }
            }
        if (io->uvs && io->vertexCapacity)
            for (uint32_t i = 0; i < std::min(vertexCount, io->vertexCapacity); ++i) {
                if (hasUv) std::memcpy(io->uvs + i * 2, bytes + size_t(i) * stride + uvOffset, sizeof(float) * 2);
                else { io->uvs[i * 2] = 0.0f; io->uvs[i * 2 + 1] = 0.0f; }
            }
        if (io->indices && io->indexCapacity)
            std::memcpy(io->indices, cpu->indices.data(), sizeof(uint32_t) * std::min(indexCount, io->indexCapacity));
        if (io->slices && io->sliceCapacity)
            for (uint32_t i = 0; i < std::min<uint32_t>(uint32_t(submeshes.size()), io->sliceCapacity); ++i) {
                io->slices[i].indexOffset = uint32_t(submeshes[i].indexOffset);
                io->slices[i].indexCount = uint32_t(submeshes[i].indexCount);
                io->slices[i].materialSlot = submeshes[i].materialIndex;
            }
        return SHARD_OK;
    }

    namespace {
        // The image as 8 bit RGBA, rows from the top
        ShardResult Decode(int32_t id, std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height)
        {
            auto infos = InfosOf(id);
            if (!infos || infos->baseInfos.type != Filesystem::Type::T_IMAGE) return SHARD_E_NOT_FOUND;
            const Rendering::TextureDecodeResult decoded = Rendering::DecodeTextureFile(infos->baseInfos.path);
            if (!decoded.success || decoded.width <= 0 || decoded.height <= 0) return SHARD_E_NOT_FOUND;
            width = uint32_t(decoded.width);
            height = uint32_t(decoded.height);
            const size_t pixels = size_t(width) * height;
            size_t channels = 0;
            switch (decoded.format) {
                case Rendering::TextureInternalFormat::RED: channels = 1; break;
                case Rendering::TextureInternalFormat::RG: channels = 2; break;
                case Rendering::TextureInternalFormat::RGB: channels = 3; break;
                case Rendering::TextureInternalFormat::RGBA: channels = 4; break;
                default: return SHARD_E_UNSUPPORTED;
            }
            if (decoded.pixels.size() < pixels * channels) return SHARD_E_UNSUPPORTED;
            rgba.resize(pixels * 4);
            for (uint32_t y = 0; y < height; ++y) {
                // The decoder flips the image (bottom row first) : the ABI wants the top row first
                const uint8_t* src = decoded.pixels.data() + size_t(height - 1 - y) * width * channels;
                uint8_t* dst = rgba.data() + size_t(y) * width * 4;
                for (uint32_t x = 0; x < width; ++x, src += channels, dst += 4) {
                    switch (channels) {
                        case 1: dst[0] = dst[1] = dst[2] = src[0]; dst[3] = 255; break;
                        case 2: dst[0] = dst[1] = dst[2] = src[0]; dst[3] = src[1]; break;
                        case 3: dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255; break;
                        default: dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3]; break;
                    }
                }
            }
            return SHARD_OK;
        }
    }

    ShardResult AssetImageInfo(int32_t id, uint32_t* width, uint32_t* height)
    {
        std::vector<uint8_t> rgba;
        return Decode(id, rgba, *width, *height);
    }

    ShardResult AssetImageRead(int32_t id, uint8_t* out, uint64_t capacity, uint64_t* size)
    {
        std::vector<uint8_t> rgba;
        uint32_t width = 0, height = 0;
        const ShardResult r = Decode(id, rgba, width, height);
        if (r != SHARD_OK) return r;
        *size = rgba.size();
        if (out && capacity) std::memcpy(out, rgba.data(), size_t(std::min<uint64_t>(capacity, rgba.size())));
        return SHARD_OK;
    }
}
