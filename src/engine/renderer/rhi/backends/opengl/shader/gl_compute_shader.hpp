#pragma once

#include "engine/renderer/rhi/shader/compute_shader.hpp"

namespace Shard::Engine::Rendering{

    class GLComputeShader : public ComputeShader{
        public:
            GLComputeShader(const Filesystem::Path &path);

            ~GLComputeShader();

            void Bind();
            void Unbind();

            std::vector<UniformInfo> GetActiveUniforms() override;
            std::vector<SamplerInfo> GetActiveSamplers() override;

            const std::unordered_map<std::string, UniformInfo>& GetActiveUniformsMap() override;
            const std::unordered_map<std::string, SamplerInfo>& GetActiveSamplersMap() override;

            void SetBool(const std::string& name, bool value) override;
            void SetInt(const std::string& name, int value) override;
            void SetFloat(const std::string& name, float value) override;

            void SetVec2(const std::string& name, const glm::vec2& value) override;
            void SetVec3(const std::string& name, const glm::vec3& value) override;
            void SetVec4(const std::string& name, const glm::vec4& value) override;

            void SetMat2(const std::string& name, const glm::mat2& mat) override;
            void SetMat3(const std::string& name, const glm::mat3& mat) override;
            void SetMat4(const std::string& name, const glm::mat4& mat) override;

            uint32_t GetProgram() const { return m_Program; }

            bool Reload() override;

            const uint32_t* GetLocalSize() const { return m_LocalSize; }

        private:

            void CompileErrors(unsigned int shader, const char *type);

            void ValidateLocalSize();

            int32_t GetUniformLocationCached(const std::string& name);

            uint32_t m_Program = 0;
            bool m_CompileFailed = false;
            uint32_t m_LocalSize[3] = { 0, 0, 0 };
            std::vector<UniformInfo> m_ActiveUniforms;
            std::unordered_map<std::string, UniformInfo> m_ActiveUniformsMap;
            std::vector<SamplerInfo> m_ActiveSamplers;
            std::unordered_map<std::string, SamplerInfo> m_ActiveSamplersMap;

            std::unordered_map<std::string, int32_t> m_UnmappedLocations;
    };

}