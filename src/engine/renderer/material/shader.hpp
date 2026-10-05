#pragma once

#include "engine/renderer/utils.hpp"

#include "engine/assets/vfs/filesystem.hpp"

namespace Shard::Engine::Rendering {

    enum class ShaderDataType
    {   
        None         = 0,
        Bool,
        Int,
        Float,

        Vec2,
        Vec3,
        Vec4,

        Mat2,
        Mat3,
        Mat4,

        UInt,
        UVec2
    };

    enum class ShaderSamplerType
    {
        None = 0,

        Texture1D,
        Texture2D,
        Texture3D,
        TextureCube,
        Texture2DMultisample,

        Texture1DArray,
        Texture2DArray,
        TextureCubeArray,
        Texture2DMultisampleArray,

        // Shadow samplers
        Texture1DShadow,
        Texture2DShadow,
        TextureCubeShadow,

        Texture1DArrayShadow,
        Texture2DArrayShadow,
        TextureCubeArrayShadow
    };

    static uint32_t ShaderDataTypeSize(ShaderDataType type)
    {
        switch(type)
        {
            case ShaderDataType::Float: return sizeof(float);
            case ShaderDataType::Bool:  return sizeof(bool);
            case ShaderDataType::Int:   return sizeof(int);

            case ShaderDataType::Vec2:  return sizeof(glm::vec2);
            case ShaderDataType::Vec3:  return sizeof(glm::vec3);
            case ShaderDataType::Vec4:  return sizeof(glm::vec4);

            case ShaderDataType::Mat2:  return sizeof(glm::mat2);
            case ShaderDataType::Mat3:  return sizeof(glm::mat3);
            case ShaderDataType::Mat4:  return sizeof(glm::mat4);

            case ShaderDataType::UInt:  return sizeof(uint32_t);
            case ShaderDataType::UVec2: return sizeof(glm::uvec2);
        }

        return 0;
    }

    static uint32_t ShaderDataTypeComponentCount(ShaderDataType type)
    {
        switch(type)
        {
            case ShaderDataType::Float: return 1;
            case ShaderDataType::Bool:  return 1;
            case ShaderDataType::Int:   return 1;

            case ShaderDataType::Vec2:  return 2;
            case ShaderDataType::Vec3:  return 3;
            case ShaderDataType::Vec4:  return 4;

            case ShaderDataType::Mat2:  return 4;
            case ShaderDataType::Mat3:  return 9;
            case ShaderDataType::Mat4:  return 16;

            case ShaderDataType::UInt:  return 1;
            case ShaderDataType::UVec2: return 2;
        }

        return 0;
    }

    struct UniformInfo
    {
        std::string name;
        ShaderDataType type;
        int32_t location = -1;
    };

    struct SamplerInfo
    {
        std::string name;
        ShaderSamplerType type;
        int32_t binding = -1;
    };

    class CommandBuffer;

    class Shader
    {
        public:

            virtual ~Shader() = default;
            
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

            virtual void SetUVec2(const std::string& name, uint32_t x, uint32_t y) = 0;

            static std::shared_ptr<Shader> Create(
                const Filesystem::Path& vertexPath,
                const Filesystem::Path& fragmentPath,
                const Filesystem::Path& geometryPath = Filesystem::Path(""));

            std::string GetVertexShaderName() const { return m_VertexFilePath; }
            std::string GetFragmentShaderName() const { return m_FragmentFilePath; }
            std::string GetGeometryShaderName() const { return m_GeometryFilePath; }

            void SetAssetID(Filesystem::AssetID id)
            {
                m_AssetID = id;
            }

            Filesystem::AssetID GetAssetID() {
                return m_AssetID;
            }

        protected:
        
            Filesystem::AssetID m_AssetID;

            std::string m_VertexFilePath;
            std::string m_FragmentFilePath;
            std::string m_GeometryFilePath;
    };

}