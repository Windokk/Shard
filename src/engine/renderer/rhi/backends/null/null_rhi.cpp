#include "engine/renderer/rhi/backends/null/null_rhi.hpp"

#include <algorithm>
#include <cstring>

namespace Shard::Engine::Rendering::Null {

    uint32_t NextHandle()
    {
        static std::atomic<uint32_t> counter{0};
        return counter.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    // ---- renderer api -------------------------------------------------------------------------------------------

    void NullRendererAPI::ExecuteDrawCommand(const DrawCommand& command, const std::shared_ptr<RenderPass> pass)
    {
        (void)command;
        (void)pass;
        ++m_Draws;
    }

    void NullRendererAPI::ExecuteComputeDispatch(const std::shared_ptr<ComputePipeline> pipeline, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        (void)pipeline;
        (void)groupsX;
        (void)groupsY;
        (void)groupsZ;
        ++m_Dispatches;
    }

    // ---- shaders, materials ---------------------------------------------------------------------------------------

    NullShader::NullShader(const Filesystem::Path& vertexPath, const Filesystem::Path& fragmentPath, const Filesystem::Path& geometryPath, const std::vector<std::string>& defines)
    {
        m_Defines = defines;
        m_VertexFilePath = vertexPath.full;
        m_FragmentFilePath = fragmentPath.full;
        m_GeometryFilePath = geometryPath.full;
    }

    NullComputeShader::NullComputeShader(const Filesystem::Path& path)
    {
        m_FilePath = path.full;
    }

    NullMaterial::NullMaterial(std::shared_ptr<Shader> shader, std::shared_ptr<Pipeline> pipeline, bool receivesShadows, Opacity opacity)
    {
        m_Shader = std::move(shader);
        m_Pipeline = std::move(pipeline);
        m_ReceivesShadows = receivesShadows;
        m_Opacity = opacity;

        // The blend state a material of this opacity draws with (the same rules as every backend's material)
        PipelineSpecifications& specs = m_Pipeline->GetSpecifications();
        switch (m_Opacity)
        {
            case Opacity::Masked:
                m_ScalarParameters["masked"] = true;
                specs.blending = false;
                [[fallthrough]];
            case Opacity::Translucent:
                specs.blending = true;
                specs.srcBlend = BlendFactor::SrcAlpha;
                specs.dstBlend = BlendFactor::OneMinusSrcAlpha;
                break;
            case Opacity::Opaque:
            default:
                specs.blending = false;
                break;
        }
    }

    std::optional<NumericValue> NullMaterial::GetScalarParameter(const std::string& name)
    {
        auto it = m_ScalarParameters.find(name);
        if (it == m_ScalarParameters.end())
            return std::nullopt;
        return it->second;
    }

    uint64_t NullMaterial::GetTextureParameter(const std::string& name) const
    {
        // There is no resident handle to give : the id of the texture the slot holds says what is bound
        auto it = m_SamplersParameters.find(name);
        return it == m_SamplersParameters.end() ? 0 : it->second;
    }

    // ---- meshes ---------------------------------------------------------------------------------------------------

    void NullMesh::Create(std::vector<uint8_t> vertices, std::vector<uint32_t> indices, const VertexLayout& layout)
    {
        const uint32_t stride = layout.GetStride();

        m_Submeshes.clear();
        m_Submeshes.push_back({0, indices.size(), stride ? vertices.size() / stride : 0});

        m_VertexCount = stride ? vertices.size() / stride : 0;
        m_VertexLayout = layout;
        m_BoundsMin = glm::vec3(std::numeric_limits<float>::max());
        m_BoundsMax = glm::vec3(std::numeric_limits<float>::lowest());

        auto posIt = std::find_if(layout.GetElements().begin(), layout.GetElements().end(),
                                  [](const VertexElement& e) { return e.name == "aPos"; });
        if (posIt != layout.GetElements().end() && stride > 0)
        {
            for (size_t i = 0; i < m_VertexCount; ++i)
            {
                glm::vec3 position;
                std::memcpy(&position, vertices.data() + i * stride + posIt->offset, sizeof(glm::vec3));
                m_BoundsMin = glm::min(m_BoundsMin, position);
                m_BoundsMax = glm::max(m_BoundsMax, position);
            }
        }
        m_Submeshes[0].boundsMin = m_BoundsMin;
        m_Submeshes[0].boundsMax = m_BoundsMax;

        m_StoredVertices = std::move(vertices);
        m_StoredIndices = std::move(indices);
        m_IndexCount = m_StoredIndices.size();
    }

    void NullMesh::CreateFromFBX(const ufbx_mesh* ufbx_mesh, double scene_unit_meters, ufbx_material_list& ufbx_mats, ufbx_node* mesh_node, COL_RGBA vertexColor)
    {
        CreateFromData(BuildMeshCPUDataFromFBX(ufbx_mesh, scene_unit_meters, ufbx_mats, mesh_node, vertexColor));
    }

    void NullMesh::CreateFromData(const MeshCPUData& data)
    {
        m_Submeshes = data.submeshes;
        m_VertexLayout = data.layout;
        m_VertexCount = data.vertices.size() / (data.layout.GetStride() ? data.layout.GetStride() : 1);
        m_BoundsMin = data.boundsMin;
        m_BoundsMax = data.boundsMax;
        m_StoredVertices = data.vertices;
        m_StoredIndices = data.indices;
        m_IndexCount = m_StoredIndices.size();
    }

    bool NullMesh::ReadBackCPUData(MeshCPUCopy& out)
    {
        out.vertices = m_StoredVertices;
        out.indices = m_StoredIndices;
        return true;
    }

    // ---- textures -------------------------------------------------------------------------------------------------

    NullTexture2D::NullTexture2D(const TextureSpecifications& specs, const void* data)
        : m_Handle(NextHandle())
    {
        (void)data;
        m_Specifications = specs;
    }

    NullTexture2D::NullTexture2D(const TextureSpecifications& specs, const CompressedImage& image, uint32_t firstMip)
        : m_Handle(NextHandle())
    {
        firstMip = std::min<uint32_t>(firstMip, image.mips.empty() ? 0u : uint32_t(image.mips.size()) - 1);
        m_Specifications = specs;
        m_Specifications.width = std::max(1u, image.width >> firstMip);
        m_Specifications.height = std::max(1u, image.height >> firstMip);
        switch (image.format)
        {
            case BlockFormat::BC4: m_Specifications.internalFormat = TextureInternalFormat::RED; break;
            case BlockFormat::BC5: m_Specifications.internalFormat = TextureInternalFormat::RG; break;
            case BlockFormat::BC7: m_Specifications.internalFormat = TextureInternalFormat::RGBA; break;
        }
    }

    void NullTexture2D::ReadPixels(void* outData, size_t bufferSize, uint32_t world) const
    {
        (void)world;
        if (outData && bufferSize)
            std::memset(outData, 0, bufferSize);
    }

    bool NullTexture2D::SwapContents(Texture2D& other)
    {
        NullTexture2D* theOther = dynamic_cast<NullTexture2D*>(&other);
        if (!theOther)
            return false;
        std::swap(m_Handle, theOther->m_Handle);
        std::swap(m_Specifications, theOther->m_Specifications);
        return true;
    }

    NullEnvironmentMap::NullEnvironmentMap(const TextureSpecifications& infos)
    {
        m_Infos = infos;
        TextureSpecifications face = infos;
        face.width = std::max(1u, infos.width);
        face.height = std::max(1u, infos.height);
        m_Cubemap = std::make_shared<NullCubemap>(face);
        m_IrradianceMap = std::make_shared<NullCubemap>(face);
        m_PrefilterMap = std::make_shared<NullCubemap>(face);

        TextureSpecifications lut;
        lut.width = lut.height = 512;
        lut.internalFormat = TextureInternalFormat::RG16F;
        m_BrdfLUT = std::make_shared<NullTexture2D>(lut, nullptr);
    }

    std::shared_ptr<EnvironmentMap> NullEnvironmentMapGenerator::GenerateFromFiles(TextureSpecifications& specs, const std::vector<Filesystem::Path> imageFiles)
    {
        (void)imageFiles;
        return std::make_shared<NullEnvironmentMap>(specs);
    }

    std::shared_ptr<EnvironmentMap> NullEnvironmentMapGenerator::GenerateFromHDR(TextureSpecifications& specs, const Filesystem::Path hdrFile)
    {
        (void)hdrFile;
        return std::make_shared<NullEnvironmentMap>(specs);
    }

    // ---- buffers, framebuffers --------------------------------------------------------------------------------------

    void NullStorageBuffer::SetData(const void* data, uint32_t size)
    {
        m_Data.assign(size, 0);
        if (data && size)
            std::memcpy(m_Data.data(), data, size);
    }

    void NullStorageBuffer::GetData(void* outData, uint32_t size, uint32_t offset) const
    {
        if (!outData || offset >= m_Data.size())
            return;
        const size_t count = std::min<size_t>(size, m_Data.size() - offset);
        std::memcpy(outData, m_Data.data() + offset, count);
    }

    NullFramebuffer::NullFramebuffer(const FramebufferSpecifications& specs)
    {
        m_Specifications = specs;
    }

    bool NullFramebuffer::ReadPixelsRGBA8(std::vector<uint8_t>& outPixels)
    {
        if (!m_Specifications.hasColor)
            return false;
        outPixels.assign(size_t(m_Specifications.width) * m_Specifications.height * 4, 0);
        return true;
    }
}
