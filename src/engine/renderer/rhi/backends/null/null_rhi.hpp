#pragma once

#include <array>
#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/pipelines/compute_pipeline.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/rhi/resources/buffer/storage_buffer.hpp"
#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/cubemap.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/envmap.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/shader/compute_shader.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"

/// The null graphics backend : every object of the rhi exists and keeps what it was told, nothing is drawn and no graphics
/// API, GPU or window is touched. It is what a headless run (a dedicated server, the simulation tests, a replay checked in
/// the CI) boots the renderer on : the whole frame - extraction, culling, pass setup, draw lists - runs, so the CPU side of
/// the renderer is exercised, only the calls that would reach the GPU end here.
///
/// There is no graphics thread : everything runs on the calling thread.
namespace Shard::Engine::Rendering::Null {

    /// A name that stands for a GPU object. Never 0 (code treats 0 as "no object"), unique for the run.
    uint32_t NextHandle();

    class NullRendererAPI final : public RendererAPI {
    public:
        NullRendererAPI() { api = API::Null; }

        void ExecuteDrawCommand(const DrawCommand& command, const std::shared_ptr<RenderPass> pass) override;
        void ExecuteComputeDispatch(const std::shared_ptr<ComputePipeline> pipeline, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) override;
        void MemoryBarrier(MemoryBarrierBit barriers) override { (void)barriers; }
        const ComputeLimits& GetComputeLimits() override { return m_Limits; }
        void ToggleMultisampling(const bool on) override { (void)on; }
        void BindTextureUnit(uint32_t binding, uint32_t handle) override { (void)binding; (void)handle; }
        void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override { (void)x; (void)y; (void)width; (void)height; }
        void SetClearColor(float r, float g, float b, float a) override { m_ClearColor = glm::vec4(r, g, b, a); }
        glm::vec4 GetClearColor() override { return m_ClearColor; }
        void Clear(ClearBit clearBits) override { (void)clearBits; }
        void SetIndirectDrawBuffer(uint32_t bufferHandle) override { (void)bufferHandle; }
        void InvalidateStateCache() override {}

        std::string GetDeviceVendor() override { return "Shard"; }
        std::string GetRendererName() override { return "Null renderer (no GPU)"; }
        std::string GetDriverVersion() override { return "none"; }

        /// How many draws and dispatches the renderer asked for since the start : what a headless test can look at
        uint64_t DrawCount() const { return m_Draws; }
        uint64_t DispatchCount() const { return m_Dispatches; }

    private:
        ComputeLimits m_Limits = [] {
            ComputeLimits limits;
            limits.maxWorkGroupCount[0] = limits.maxWorkGroupCount[1] = limits.maxWorkGroupCount[2] = 65535;
            limits.maxWorkGroupSize[0] = limits.maxWorkGroupSize[1] = 1024;
            limits.maxWorkGroupSize[2] = 64;
            limits.maxWorkGroupInvocations = 1024;
            limits.maxSharedMemorySize = 32768;
            return limits;
        }();
        glm::vec4 m_ClearColor = glm::vec4(0.0f);
        uint64_t m_Draws = 0;
        uint64_t m_Dispatches = 0;
    };

    class NullShader final : public Shader {
    public:
        NullShader(const Filesystem::Path& vertexPath, const Filesystem::Path& fragmentPath, const Filesystem::Path& geometryPath, const std::vector<std::string>& defines = {});

        std::vector<UniformInfo> GetActiveUniforms() override { return {}; }
        const std::unordered_map<std::string, UniformInfo>& GetActiveUniformsMap() override { return m_Uniforms; }
        std::vector<SamplerInfo> GetActiveSamplers() override { return {}; }
        const std::unordered_map<std::string, SamplerInfo>& GetActiveSamplersMap() override { return m_Samplers; }

        void SetBool(const std::string&, bool) override {}
        void SetInt(const std::string&, int) override {}
        void SetFloat(const std::string&, float) override {}
        void SetVec2(const std::string&, const glm::vec2&) override {}
        void SetVec3(const std::string&, const glm::vec3&) override {}
        void SetVec4(const std::string&, const glm::vec4&) override {}
        void SetMat2(const std::string&, const glm::mat2&) override {}
        void SetMat3(const std::string&, const glm::mat3&) override {}
        void SetMat4(const std::string&, const glm::mat4&) override {}
        void SetUVec2(const std::string&, uint32_t, uint32_t) override {}

        bool Reload() override { return true; }

    private:
        std::unordered_map<std::string, UniformInfo> m_Uniforms;
        std::unordered_map<std::string, SamplerInfo> m_Samplers;
    };

    class NullComputeShader final : public ComputeShader {
    public:
        explicit NullComputeShader(const Filesystem::Path& path);

        std::vector<UniformInfo> GetActiveUniforms() override { return {}; }
        const std::unordered_map<std::string, UniformInfo>& GetActiveUniformsMap() override { return m_Uniforms; }
        std::vector<SamplerInfo> GetActiveSamplers() override { return {}; }
        const std::unordered_map<std::string, SamplerInfo>& GetActiveSamplersMap() override { return m_Samplers; }

        void SetBool(const std::string&, bool) override {}
        void SetInt(const std::string&, int) override {}
        void SetFloat(const std::string&, float) override {}
        void SetVec2(const std::string&, const glm::vec2&) override {}
        void SetVec3(const std::string&, const glm::vec3&) override {}
        void SetVec4(const std::string&, const glm::vec4&) override {}
        void SetMat2(const std::string&, const glm::mat2&) override {}
        void SetMat3(const std::string&, const glm::mat3&) override {}
        void SetMat4(const std::string&, const glm::mat4&) override {}

        bool Reload() override { return true; }

    private:
        std::unordered_map<std::string, UniformInfo> m_Uniforms;
        std::unordered_map<std::string, SamplerInfo> m_Samplers;
    };

    class NullPipeline final : public Pipeline {
    public:
        explicit NullPipeline(const PipelineSpecifications& specs) { m_Specifications = specs; }
        void Invalidate() override {}
        void Bind() override {}
    };

    class NullComputePipeline final : public ComputePipeline {
    public:
        explicit NullComputePipeline(const ComputePipelineSpecifications& specs) { m_Specifications = specs; }
        void Bind() override {}
    };

    /// Keeps every parameter it is given (the null shader has no uniform list to check them against)
    class NullMaterial final : public Material {
    public:
        NullMaterial(std::shared_ptr<Shader> shader, std::shared_ptr<Pipeline> pipeline, bool receivesShadows, Opacity opacity);

        void SetScalarParameter(const std::string& name, const NumericValue& value) override { m_ScalarParameters[name] = value; }
        void SetTextureParameter(const std::string& name, uint64_t texture) override { m_SamplersParameters[name] = static_cast<uint32_t>(texture); }
        std::optional<NumericValue> GetScalarParameter(const std::string& name) override;
        uint64_t GetTextureParameter(const std::string& name) const override;
        uint32_t GetTexturesCount() const override { return static_cast<uint32_t>(m_SamplersParameters.size()); }
    };

    /// The geometry stays on the CPU (there is no GPU to move it to), so reading it back always works
    class NullMesh final : public Mesh {
    public:
        void Create(std::vector<uint8_t> vertices, std::vector<uint32_t> indices, const VertexLayout& layout) override;
        void CreateFromFBX(const ufbx_mesh* ufbx_mesh, double scene_unit_meters, ufbx_material_list& ufbx_mats, ufbx_node* mesh_node, COL_RGBA vertexColor) override;
        void CreateFromData(const MeshCPUData& data) override;

    protected:
        bool ReadBackCPUData(MeshCPUCopy& out) override;

    private:
        std::vector<uint8_t> m_StoredVertices;
        std::vector<uint32_t> m_StoredIndices;
    };

    class NullTexture2D final : public Texture2D {
    public:
        NullTexture2D(const TextureSpecifications& specs, const void* data);
        NullTexture2D(const TextureSpecifications& specs, const CompressedImage& image, uint32_t firstMip);

        void Bind(uint32_t slot = 0) const override { (void)slot; }
        void BindImage(uint32_t unit, TextureAccess access, uint32_t world = 0) const override { (void)unit; (void)access; (void)world; }
        void ReadPixels(void* outData, size_t bufferSize, uint32_t world = 0) const override;
        uint32_t GetHandle() const override { return m_Handle; }
        bool IsValid() const override { return true; }
        bool SwapContents(Texture2D& other) override;

    private:
        uint32_t m_Handle;
    };

    class NullCubemap final : public Cubemap {
    public:
        explicit NullCubemap(const TextureSpecifications& specs) { m_Specifications = specs; }
        void Bind(uint32_t slot = 0) const override { (void)slot; }
        uint32_t GetHandle() const override { return m_Handle; }
        bool IsValid() const override { return true; }
    private:
        uint32_t m_Handle = NextHandle();
    };

    class NullCubemapArray final : public CubemapArray {
    public:
        NullCubemapArray(const TextureSpecifications& specs, uint32_t count) : m_Count(count) { m_Specifications = specs; }
        void Bind(uint32_t slot = 0) const override { (void)slot; }
        uint32_t GetCount() const override { return m_Count; }
        uint32_t GetHandle() const override { return m_Handle; }
        bool IsValid() const override { return true; }
    private:
        uint32_t m_Count;
        uint32_t m_Handle = NextHandle();
    };

    class NullEnvironmentMap final : public EnvironmentMap {
    public:
        explicit NullEnvironmentMap(const TextureSpecifications& infos);
    };

    class NullEnvironmentMapGenerator final : public EnvironmentMapGenerator {
    public:
        std::shared_ptr<EnvironmentMap> GenerateFromFiles(TextureSpecifications& specs, const std::vector<Filesystem::Path> imageFiles) override;
        std::shared_ptr<EnvironmentMap> GenerateFromHDR(TextureSpecifications& specs, const Filesystem::Path hdrFile) override;
    };

    class NullStorageBuffer final : public StorageBuffer {
    public:
        explicit NullStorageBuffer(uint32_t size) : m_Data(size, 0) {}
        void SetData(const void* data, uint32_t size) override;
        void GetData(void* outData, uint32_t size, uint32_t offset = 0) const override;
        void Bind(uint32_t binding) override { (void)binding; }
        uint32_t GetSize() const override { return static_cast<uint32_t>(m_Data.size()); }
        uint32_t GetHandle() const override { return m_Handle; }
    private:
        std::vector<uint8_t> m_Data;
        uint32_t m_Handle = NextHandle();
    };

    class NullFramebuffer final : public Framebuffer {
    public:
        explicit NullFramebuffer(const FramebufferSpecifications& specs);

        void Destroy() override { m_Valid = false; }
        void Bind() override {}
        void Unbind() override {}
        void ResolveMultisampled() override {}
        void Resize(uint32_t width, uint32_t height) override { m_Specifications.width = width; m_Specifications.height = height; }
        void SetSamplesCount(uint32_t samples) override { m_Specifications.samplesCount = samples; }
        void AttachCubemapArray(uint32_t texture) override { (void)texture; }
        void DetachCubemapArray() override {}
        void CopyFrom(std::shared_ptr<Framebuffer> src) override { (void)src; }
        void CopyDepthFrom(std::shared_ptr<Framebuffer> src) override { (void)src; }
        void BlitToScreen(uint32_t screenWidth, uint32_t screenHeight) override { (void)screenWidth; (void)screenHeight; }
        /// Nothing was drawn : the picture is black
        bool ReadPixelsRGBA8(std::vector<uint8_t>& outPixels) override;
        void UploadColorRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const uint8_t* rgba) override { (void)x; (void)y; (void)width; (void)height; (void)rgba; }
        void BlitColorTo(Framebuffer& dst, uint32_t dstX, uint32_t dstY, uint32_t dstWidth, uint32_t dstHeight) override { (void)dst; (void)dstX; (void)dstY; (void)dstWidth; (void)dstHeight; }

        uint32_t GetColorAttachment() const override { return m_Specifications.hasColor ? m_Color : 0; }
        uint32_t GetDepthAttachment() const override { return m_Specifications.hasDepth ? m_Depth : 0; }
        uint32_t GetResolveColorAttachment() const override { return m_Specifications.hasColor ? m_Color : 0; }
        uint32_t GetResolveDepthAttachment() const override { return m_Specifications.hasDepth ? m_Depth : 0; }
        uint64_t GetColorAttachmentBindlessHandle() const override { return 0; }
        uint32_t GetHandle() const override { return m_Handle; }
        bool IsValid() const override { return m_Valid; }

    private:
        uint32_t m_Handle = NextHandle();
        uint32_t m_Color = NextHandle();
        uint32_t m_Depth = NextHandle();
        bool m_Valid = true;
    };
}
