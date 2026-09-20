#include "thumbnail_service.hpp"

#include "engine/core/engine.hpp"
#include "engine/core/resources/resources_manager.hpp"

#include "engine/rendering/immediate/immediate_renderer.hpp"
#include "engine/rendering/material/material.hpp"
#include "engine/rendering/mesh/mesh.hpp"
#include "engine/rendering/renderer/renderer.hpp"
#include "engine/rendering/shader/shader.hpp"
#include "engine/rendering/texture/image_export.hpp"
#include "engine/serialization/material/material_serializer.hpp"

#include "engine/debugging/logger.hpp"

#include <stb/stb_image.h>

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace Shard::Engine::Rendering {

    namespace {
        // Same background the mesh-only thumbnails always had, so the asset browser's look doesn't change.
        const glm::vec4 kBackground(0.145f, 0.145f, 0.157f, 1.0f);

        uint64_t Fnv1a64(const std::string& text)
        {
            uint64_t hash = 1469598103934665603ULL;
            for (unsigned char c : text)
            {
                hash ^= c;
                hash *= 1099511628211ULL;
            }
            return hash;
        }
    }

    void ThumbnailService::Init(Renderer* renderer)
    {
        m_Renderer = renderer;
    }

    void ThumbnailService::Shutdown()
    {
        if (m_Atlas)
            m_Atlas->Destroy();
        m_Atlas = nullptr;
        m_Entries.clear();
        m_SlotKeys.clear();
        m_PreviewSphere = nullptr;
    }

    void ThumbnailService::BeginFrame()
    {
        ++m_Frame;
        m_RendersThisFrame = 0;
        m_DiskLoadsThisFrame = 0;
    }

    std::string ThumbnailService::MakeKey(ThumbnailKind kind, const std::string& nameInProject)
    {
        return std::to_string(static_cast<int>(kind)) + ":" + nameInProject;
    }

    std::string ThumbnailService::CacheFilePath(const ThumbnailRequest& request) const
    {
        std::string root = Core::GetEngine().GetFileManager()->GetProjectRoot().full;
        if (root.empty())
            return "";

        std::error_code ec;
        auto size = std::filesystem::file_size(request.filePath.full, ec);
        if (ec)
            return "";
        auto stamp = std::filesystem::last_write_time(request.filePath.full, ec);
        if (ec)
            return "";

        std::string identity = std::to_string(kRenderVersion) + "|" + std::to_string(kCellSize) + "|"
            + MakeKey(request.kind, request.nameInProject) + "|" + std::to_string(size) + "|"
            + std::to_string(stamp.time_since_epoch().count());

        char name[32];
        snprintf(name, sizeof(name), "%016llx.png", static_cast<unsigned long long>(Fnv1a64(identity)));

        return (std::filesystem::path(root) / ".cache" / "thumbnails" / name).generic_string();
    }

    int ThumbnailService::AllocateSlot(const std::string& key)
    {
        int slot = -1;

        for (size_t i = 0; i < m_SlotKeys.size(); ++i)
        {
            if (m_SlotKeys[i].empty())
            {
                slot = static_cast<int>(i);
                break;
            }
        }

        if (slot < 0)
        {
            // Atlas full : evict the least recently used thumbnail, but never one that was requested this
            // frame - it is (or is about to be) on screen, and evicting it would just make two visible
            // tiles fight over slots forever.
            uint64_t oldest = m_Frame;
            for (size_t i = 0; i < m_SlotKeys.size(); ++i)
            {
                uint64_t used = m_Entries[m_SlotKeys[i]].lastUsedFrame;
                if (used < oldest)
                {
                    oldest = used;
                    slot = static_cast<int>(i);
                }
            }

            if (slot < 0)
                return -1;

            m_Entries.erase(m_SlotKeys[slot]);
        }

        m_SlotKeys[slot] = key;
        return slot;
    }

    void ThumbnailService::FreeSlot(uint32_t slot)
    {
        m_SlotKeys[slot].clear();
    }

    ThumbnailRegion ThumbnailService::RegionOf(uint32_t slot) const
    {
        uint32_t col = slot % kGridDim;
        uint32_t row = slot / kGridDim;
        float cell = 1.0f / static_cast<float>(kGridDim);

        // V flipped (uv0 at the cell's bottom, uv1 at its top) : the atlas is stored the way GL renders
        // it, and every other render-to-texture result in the editor is shown with the same flip.
        ThumbnailRegion region;
        region.uv0 = glm::vec2(col * cell, (row + 1) * cell);
        region.uv1 = glm::vec2((col + 1) * cell, row * cell);
        return region;
    }

    bool ThumbnailService::Get(const ThumbnailRequest& request, ThumbnailRegion& out)
    {
        if (!m_Renderer)
            return false;

        if (!m_Atlas)
        {
            FramebufferSpecifications specs;
            specs.width = kAtlasSize;
            specs.height = kAtlasSize;
            specs.hasColor = true;
            specs.hasDepth = false;
            specs.multisampled = false;
            specs.colorSpecs.minFilter = TextureFilter::Linear;
            specs.colorSpecs.magFilter = TextureFilter::Linear;
            specs.colorSpecs.generateMips = false;

            m_Atlas = Framebuffer::Create(specs);
            if (!m_Atlas || !m_Atlas->IsValid())
            {
                DEBUG_ERROR("ThumbnailService : could not create the atlas");
                m_Atlas = nullptr;
                return false;
            }
            m_SlotKeys.assign(kGridDim * kGridDim, "");
        }

        std::string key = MakeKey(request.kind, request.nameInProject);

        auto it = m_Entries.find(key);
        if (it != m_Entries.end())
        {
            if (!it->second.ready)
                return false;

            it->second.lastUsedFrame = m_Frame;
            out = RegionOf(it->second.slot);
            return true;
        }

        std::string cacheFile = CacheFilePath(request);
        bool cached = !cacheFile.empty() && std::filesystem::exists(cacheFile);

        if (cached ? m_DiskLoadsThisFrame >= kMaxDiskLoadsPerFrame : m_RendersThisFrame >= kMaxRendersPerFrame)
            return false;

        int slot = AllocateSlot(key);
        if (slot < 0)
            return false;

        bool ok = false;

        if (cached)
        {
            ++m_DiskLoadsThisFrame;
            ok = TryLoadFromDisk(cacheFile, slot);
        }

        if (!ok)
        {
            if (m_RendersThisFrame >= kMaxRendersPerFrame)
            {
                FreeSlot(slot);
                return false;
            }

            ++m_RendersThisFrame;
            ok = Render(request, slot, cacheFile);
        }

        if (!ok)
        {
            // Don't retry every frame : a mesh that fails to load will keep failing.
            FreeSlot(slot);
            m_Entries[key] = Entry{};
            return false;
        }

        Entry entry;
        entry.ready = true;
        entry.slot = slot;
        entry.lastUsedFrame = m_Frame;
        entry.cacheFile = cacheFile;
        m_Entries[key] = entry;

        out = RegionOf(slot);
        return true;
    }

    void ThumbnailService::Invalidate(const ThumbnailRequest& request)
    {
        // The cache key only sees the asset file's own size/date, so an edit that leaves it untouched
        // (a texture the material uses) would otherwise come back from disk : delete the file too.
        std::error_code ec;

        auto it = m_Entries.find(MakeKey(request.kind, request.nameInProject));
        if (it != m_Entries.end())
        {
            if (it->second.ready)
                FreeSlot(it->second.slot);
            if (!it->second.cacheFile.empty())
                std::filesystem::remove(it->second.cacheFile, ec);
            m_Entries.erase(it);
        }

        std::string cacheFile = CacheFilePath(request);
        if (!cacheFile.empty())
            std::filesystem::remove(cacheFile, ec);
    }

    bool ThumbnailService::TryLoadFromDisk(const std::string& cacheFile, uint32_t slot)
    {
        std::ifstream file(cacheFile, std::ios::binary);
        if (!file)
            return false;

        std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        int width = 0, height = 0, channels = 0;
        unsigned char* pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
            static_cast<int>(bytes.size()), &width, &height, &channels, 4);
        if (!pixels)
            return false;

        bool ok = width == static_cast<int>(kCellSize) && height == static_cast<int>(kCellSize);
        if (ok)
            m_Atlas->UploadColorRegion((slot % kGridDim) * kCellSize, (slot / kGridDim) * kCellSize, kCellSize, kCellSize, pixels);

        stbi_image_free(pixels);
        return ok;
    }

    std::shared_ptr<Mesh> ThumbnailService::GetPreviewSphere()
    {
        if (m_PreviewSphere)
            return m_PreviewSphere;

        // Same vertex format the mesh importer produces (see mesh.cpp), which lit.vert expects.
        struct Vertex {
            glm::vec3 position;
            glm::vec2 texCoord;
            glm::vec3 normal;
            glm::vec4 color;
            glm::vec3 tangent;
        };

        constexpr int kRings = 24;
        constexpr int kSectors = 48;

        std::vector<Vertex> vertices;
        vertices.reserve((kRings + 1) * (kSectors + 1));
        for (int r = 0; r <= kRings; ++r)
        {
            float theta = glm::pi<float>() * r / kRings;
            for (int s = 0; s <= kSectors; ++s)
            {
                float phi = glm::two_pi<float>() * s / kSectors;
                glm::vec3 normal(sinf(theta) * cosf(phi), cosf(theta), sinf(theta) * sinf(phi));

                Vertex v;
                v.position = normal * 1.2f;
                v.texCoord = glm::vec2(static_cast<float>(s) / kSectors, static_cast<float>(r) / kRings);
                v.normal = normal;
                v.color = glm::vec4(1.0f);
                v.tangent = glm::vec3(-sinf(phi), 0.0f, cosf(phi));
                vertices.push_back(v);
            }
        }

        std::vector<uint32_t> indices;
        indices.reserve(kRings * kSectors * 6);
        for (int r = 0; r < kRings; ++r)
        {
            for (int s = 0; s < kSectors; ++s)
            {
                uint32_t a = r * (kSectors + 1) + s;
                uint32_t b = a + kSectors + 1;
                indices.insert(indices.end(), {a, a + 1, b, a + 1, b + 1, b});
            }
        }

        VertexLayout layout = {
            {{"aPos",     ShaderDataType::Vec3, 0, offsetof(Vertex, position)},
             {"aTexCoord",ShaderDataType::Vec2, 1, offsetof(Vertex, texCoord)},
             {"aNormal",  ShaderDataType::Vec3, 2, offsetof(Vertex, normal)},
             {"aColor",   ShaderDataType::Vec4, 3, offsetof(Vertex, color)},
             {"aTangent", ShaderDataType::Vec3, 4, offsetof(Vertex, tangent)}}, sizeof(Vertex)
        };

        std::vector<uint8_t> bytes(vertices.size() * sizeof(Vertex));
        memcpy(bytes.data(), vertices.data(), bytes.size());

        m_PreviewSphere = Mesh::Create();
        m_PreviewSphere->Create(bytes, indices, layout);
        return m_PreviewSphere;
    }

    bool ThumbnailService::Render(const ThumbnailRequest& request, uint32_t slot, const std::string& cacheFile)
    {
        auto* resources = Core::GetEngine().GetResourcesManager();

        std::vector<DrawCommand> commands;
        glm::vec3 boundsMin, boundsMax;

        auto makeCommand = [](const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Material>& material,
                              size_t indexOffset, size_t indexCount, size_t vertexCount)
        {
            DrawCommand command{};
            command.indexOffset = static_cast<uint32_t>(indexOffset);
            command.indexCount = static_cast<uint32_t>(indexCount);
            command.vertexCount = static_cast<uint32_t>(vertexCount);
            command.mesh = mesh;
            command.material = material;
            command.modelMatrix = glm::mat4(1.0f);
            command.boundsMin = mesh->GetBoundsMin();
            command.boundsMax = mesh->GetBoundsMax();
            return command;
        };

        if (request.kind == ThumbnailKind::Mesh)
        {
            auto mesh = resources->GetMesh(request.nameInProject);
            auto material = resources->GetMaterial("materials/preview_default.mat");
            if (!mesh || mesh->GetIndexCount() == 0 || !material)
                return false;

            for (const auto& sub : mesh->GetSubMeshes())
                commands.push_back(makeCommand(mesh, material, sub.indexOffset, sub.indexCount, sub.vertexCount));

            boundsMin = mesh->GetBoundsMin();
            boundsMax = mesh->GetBoundsMax();
        }
        else
        {
            // Built fresh from the saved file rather than taken from the resource cache : a resident
            // material keeps the parameters it was loaded with until the level reloads, so it would keep
            // showing the pre-edit look after a save or a refresh.
            auto material = Serialization::DeserializeMaterial(request.filePath);
            auto sphere = GetPreviewSphere();
            if (!material || !sphere || sphere->GetIndexCount() == 0)
                return false;

            commands.push_back(makeCommand(sphere, material, 0, sphere->GetIndexCount(), sphere->GetVertexCount()));

            boundsMin = glm::vec3(-1.0f);
            boundsMax = glm::vec3(1.0f);
        }

        if (commands.empty())
            return false;

        ImmediateDesc desc;
        desc.view = ImmediateRenderer::FrameBounds(boundsMin, boundsMax);
        desc.width = kCellSize;
        desc.height = kCellSize;
        desc.lighting = ImmediateLighting::Studio;
        desc.clearColor = kBackground;

        ImmediateRenderer* immediate = m_Renderer->GetImmediateRenderer();

        ImmediateTarget target = immediate->Render(desc, commands);
        if (!target.IsValid())
            return false;

        immediate->CopyTo(target, *m_Atlas, (slot % kGridDim) * kCellSize, (slot / kGridDim) * kCellSize, kCellSize, kCellSize);

        if (!cacheFile.empty())
        {
            std::vector<uint8_t> pixels;
            if (immediate->ReadPixels(target, pixels))
            {
                std::error_code ec;
                std::filesystem::create_directories(std::filesystem::path(cacheFile).parent_path(), ec);
                ImageExport::WritePNG(Filesystem::Path(cacheFile), kCellSize, kCellSize, 4, pixels.data());
            }
        }

        return true;
    }
}
