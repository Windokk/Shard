#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/renderer/rhi/shader/compute_shader.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/renderer/rhi/shader/shader_binary.hpp"

#include "vk_context.hpp"

namespace Shard::Engine::Rendering {

    /// @brief A compiled program on the GPU : the shader modules, the layout of its resources, and what the engine gives it by name.
    ///
    /// The uniforms a shader declares loose are one block of memory (see ShaderCompiler) : SetFloat & co write into a copy of it on the CPU, and a
    /// draw that needs them puts that copy in the uniform ring and binds the slice. The samplers and storage resources are bound to tables of
    /// the API by number, as OpenGL does with texture units and binding points (see VKRendererAPI).
    class VKProgram
    {
        public:

            VKProgram() = default;
            ~VKProgram();
            VKProgram(const VKProgram&) = delete;
            VKProgram& operator=(const VKProgram&) = delete;

            /// Loads the compiled program of these sources (compiling them if nobody has, or if they changed since) and builds what a draw needs
            bool Load(const std::vector<std::pair<ShaderStageKind, std::string>>& files, const std::string& name, const std::vector<std::string>& defines = {});

            void Release();

            bool IsValid() const { return m_Valid; }
            uint32_t GetId() const { return m_Id; }
            uint32_t GetVersion() const { return m_Version; }
            const ShaderBinary& GetBinary() const { return m_Binary; }
            VkShaderModule GetModule(ShaderStageKind kind) const { return m_Modules[static_cast<size_t>(kind)]; }
            VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }
            VkDescriptorSetLayout GetSetLayout(uint32_t set) const { return m_SetLayouts[set]; }
            const std::string& GetName() const { return m_Name; }

            // ---- What is set by name ----

            /// Writes `size` bytes at the uniform `name` (a no-op if the program has none by that name : the engine sets uniforms every shader may not have)
            bool WriteUniform(const std::string& name, const void* data, uint32_t size, uint32_t stride = 0, uint32_t columns = 0);
            bool HasUniform(const std::string& name) const;

            /// The sampler with this name reads the texture of unit `unit` from now on (OpenGL : glUniform1i on a sampler)
            bool SetSamplerUnit(const std::string& name, uint32_t unit);

            /// The unit a sampler binding reads : its own binding number unless the engine said otherwise
            uint32_t GetSamplerUnit(const ReflectedBinding& binding, uint32_t element) const;

            /// Puts the uniforms in the ring of the frame if they changed since they were last put there. False : the program has none.
            bool FlushUniforms(uint32_t& dynamicOffset);

            VkDescriptorSet GetUniformSet() const { return m_UniformSet; }

            const std::unordered_map<std::string, UniformInfo>& GetUniformMap() const { return m_UniformInfos; }
            const std::unordered_map<std::string, SamplerInfo>& GetSamplerMap() const { return m_SamplerInfos; }

            const uint32_t* GetLocalSize() const { return m_Binary.reflection.localSize; }

        private:

            bool Build();
            bool BuildLayouts();
            void IndexReflection();

            bool m_Valid = false;
            uint32_t m_Id = 0;
            uint32_t m_Version = 0;
            std::string m_Name;
            ShaderBinary m_Binary;
            std::array<VkShaderModule, static_cast<size_t>(ShaderStageKind::Count)> m_Modules{};
            std::array<VkDescriptorSetLayout, BindingSets::Count> m_SetLayouts{};
            VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
            VkDescriptorSet m_UniformSet = VK_NULL_HANDLE;

            std::vector<uint8_t> m_Shadow;
            bool m_Dirty = true;
            uint64_t m_UploadedFrame = ~0ull;
            uint32_t m_UploadedOffset = 0;

            std::unordered_map<std::string, ReflectedUniform> m_Uniforms;       // by name, array elements by their own name (and the array by its first)
            std::unordered_map<std::string, UniformInfo> m_UniformInfos;
            std::unordered_map<std::string, SamplerInfo> m_SamplerInfos;
            std::unordered_map<uint32_t, uint32_t> m_SamplerUnits;              // binding -> unit
            std::unordered_map<std::string, uint32_t> m_SamplerBindingByName;
    };

    class VKShader final : public Shader
    {
        public:

            VKShader(const Filesystem::Path& vertexPath, const Filesystem::Path& fragmentPath, const Filesystem::Path& geometryPath, const std::vector<std::string>& defines = {});

            std::vector<UniformInfo> GetActiveUniforms() override;
            const std::unordered_map<std::string, UniformInfo>& GetActiveUniformsMap() override { return m_Program.GetUniformMap(); }
            std::vector<SamplerInfo> GetActiveSamplers() override;
            const std::unordered_map<std::string, SamplerInfo>& GetActiveSamplersMap() override { return m_Program.GetSamplerMap(); }

            void SetBool(const std::string& name, bool value) override;
            void SetInt(const std::string& name, int value) override;
            void SetFloat(const std::string& name, float value) override;
            void SetVec2(const std::string& name, const glm::vec2& value) override;
            void SetVec3(const std::string& name, const glm::vec3& value) override;
            void SetVec4(const std::string& name, const glm::vec4& value) override;
            void SetMat2(const std::string& name, const glm::mat2& mat) override;
            void SetMat3(const std::string& name, const glm::mat3& mat) override;
            void SetMat4(const std::string& name, const glm::mat4& mat) override;
            void SetUVec2(const std::string& name, uint32_t x, uint32_t y) override;

            bool Reload() override;

            VKProgram& Program() { return m_Program; }

        private:

            VKProgram m_Program;
    };

    class VKComputeShader final : public ComputeShader
    {
        public:

            explicit VKComputeShader(const Filesystem::Path& path);

            std::vector<UniformInfo> GetActiveUniforms() override;
            const std::unordered_map<std::string, UniformInfo>& GetActiveUniformsMap() override { return m_Program.GetUniformMap(); }
            std::vector<SamplerInfo> GetActiveSamplers() override;
            const std::unordered_map<std::string, SamplerInfo>& GetActiveSamplersMap() override { return m_Program.GetSamplerMap(); }

            void SetBool(const std::string& name, bool value) override;
            void SetInt(const std::string& name, int value) override;
            void SetFloat(const std::string& name, float value) override;
            void SetVec2(const std::string& name, const glm::vec2& value) override;
            void SetVec3(const std::string& name, const glm::vec3& value) override;
            void SetVec4(const std::string& name, const glm::vec4& value) override;
            void SetMat2(const std::string& name, const glm::mat2& mat) override;
            void SetMat3(const std::string& name, const glm::mat3& mat) override;
            void SetMat4(const std::string& name, const glm::mat4& mat) override;

            bool Reload() override;

            VKProgram& Program() { return m_Program; }

        private:

            VKProgram m_Program;
    };

    /// Releases the descriptor set layouts the programs shared (at shutdown, with the device)
    void ClearShaderLayoutCache();

}
