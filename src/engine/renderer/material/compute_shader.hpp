#pragma once

#include "shader.hpp"

namespace Shard::Engine::Rendering {

    class ComputeShader{
        public:

            virtual ~ComputeShader() = default;
            
            virtual std::vector<UniformInfo> GetActiveUniforms() = 0;
            virtual const std::unordered_map<std::string, UniformInfo>& GetActiveUniformsMap() = 0;
            virtual std::vector<SamplerInfo> GetActiveSamplers() = 0;
            virtual const std::unordered_map<std::string, SamplerInfo>& GetActiveSamplersMap() = 0;

            virtual void SetBool(const std::string& name, bool value) = 0;
            virtual void SetInt(const std::string& name, int value) = 0;
            virtual void SetFloat(const std::string& name, float value) = 0;

            virtual void SetVec2(const std::string& name, const glm::vec2& value) = 0;
            virtual void SetVec3(const std::string& name, const glm::vec3& value) = 0;
            virtual void SetVec4(const std::string& name, const glm::vec4& value) = 0;

            virtual void SetMat2(const std::string& name, const glm::mat2& mat) = 0;
            virtual void SetMat3(const std::string& name, const glm::mat3& mat) = 0;
            virtual void SetMat4(const std::string& name, const glm::mat4& mat) = 0;

            static std::shared_ptr<ComputeShader> Create(
                const Filesystem::Path& path);

            std::string GetShaderName() const { return m_FilePath; }

            void SetAssetID(Filesystem::AssetID id)
            {
                m_AssetID = id;
            }

            Filesystem::AssetID GetAssetID() {
                return m_AssetID;
            }

        protected:
        
            Filesystem::AssetID m_AssetID;

            std::string m_FilePath;
    };
}