#pragma once

#include "engine/renderer/material/shader.hpp"

namespace Shard::Engine::Rendering{

    class GLShader : public Shader{
        public:
            GLShader(const Filesystem::Path &vertexPath, const Filesystem::Path &fragmentPath, const Filesystem::Path &geometryPath);

            ~GLShader();

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

            void SetUVec2(const std::string& name, uint32_t x, uint32_t y) override;

            uint32_t GetProgram() const { return m_Program; }

        private:

            void CompileErrors(unsigned int shader, const char *type);

            // Uniform locations are resolved once at compile time (see the constructor) and kept
            // in m_ActiveUniformsMap; looking them up here avoids a glGetUniformLocation() driver
            // round-trip on every SetXxx call, which used to dominate per-draw-call state binding cost.
            int32_t GetUniformLocationCached(const std::string& name);

            uint32_t m_Program;
            std::vector<UniformInfo> m_ActiveUniforms;
            std::unordered_map<std::string, UniformInfo> m_ActiveUniformsMap;
            std::vector<SamplerInfo> m_ActiveSamplers;
            std::unordered_map<std::string, SamplerInfo> m_ActiveSamplersMap;

            // Locations for names m_ActiveUniformsMap doesn't cover, memoised (-1 included) so the
            // driver is still only asked once per name - see GetUniformLocationCached.
            std::unordered_map<std::string, int32_t> m_UnmappedLocations;
    };

}