#include "gl_compute_shader.hpp"

#include "engine/renderer/rhi/backends/opengl/gl_utils.hpp"
#include "engine/renderer/material/glsl_preprocessor.hpp"

#include "engine/debugging/logger.hpp"

#include "engine/core/engine.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"

namespace Shard::Engine::Rendering{

    void GLComputeShader::CompileErrors(unsigned int shader, const char* type)
    {
        GLint hasCompiled;
        char infoLog[1024];
        if (type != "PROGRAM")
        {
            glGetShaderiv(shader, GL_COMPILE_STATUS, &hasCompiled);
            if (hasCompiled == GL_FALSE)
            {
                glGetShaderInfoLog(shader, 1024, NULL, infoLog);
                DEBUG_ERROR("Couldn't compile shader : " + std::string(type) + "\n" + infoLog);
                return;
            }
        }
        else
        {
            glGetProgramiv(shader, GL_LINK_STATUS, &hasCompiled);
            if (hasCompiled == GL_FALSE)
            {
                glGetProgramInfoLog(shader, 1024, NULL, infoLog);
                DEBUG_ERROR("Couldn't link shader : " + std::string(type) + "\n" + infoLog);
                return;
            }
        }
    }

    GLComputeShader::GLComputeShader(const Filesystem::Path &path)
    {
        if (!path.Exists() || path.IsDirectory())
            return;

        m_FilePath = path.full;

        // Expand `#include "..."` directives (see glsl_preprocessor.hpp) before handing the source to
        // the driver - GLSL itself has no include support.
        std::string computeCode = ResolveGLSLIncludes(path);
        const char* computeSource = computeCode.c_str();

        GLuint computeShader = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(computeShader, 1, &computeSource, NULL);
        glCompileShader(computeShader);
        CompileErrors(computeShader, "COMPUTE");

        m_Program = glCreateProgram();
        glAttachShader(m_Program, computeShader);
        glLinkProgram(m_Program);
        CompileErrors(m_Program, "PROGRAM");

        glDeleteShader(computeShader);

        GLint localSize[3] = { 0, 0, 0 };
        glGetProgramiv(m_Program, GL_COMPUTE_WORK_GROUP_SIZE, localSize);
        m_LocalSize[0] = static_cast<uint32_t>(localSize[0]);
        m_LocalSize[1] = static_cast<uint32_t>(localSize[1]);
        m_LocalSize[2] = static_cast<uint32_t>(localSize[2]);

        ValidateLocalSize();

        GLint uniformCount;
        glGetProgramiv(m_Program, GL_ACTIVE_UNIFORMS, &uniformCount);

        GLint maxNameLength;
        glGetProgramiv(m_Program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxNameLength);

        std::vector<char> nameData(maxNameLength);

        for (GLint i = 0; i < uniformCount; ++i)
        {
            GLsizei length;
            GLint size;
            GLenum type;

            glGetActiveUniform(m_Program, i, maxNameLength, &length, &size, &type, nameData.data());

            std::string name(nameData.data(), length);

            if (name.rfind("gl_", 0) == 0)
                continue;

            if (auto pos = name.find("[0]"); pos != std::string::npos)
                name.resize(pos);

            GLint location = glGetUniformLocation(m_Program, name.c_str());

            // -------------------------
            // Data uniforms
            // -------------------------
            ShaderDataType dataType = ShaderDataType::None;

            switch (type)
            {
                case GL_BOOL:        dataType = ShaderDataType::Bool; break;
                case GL_INT:         dataType = ShaderDataType::Int; break;
                case GL_FLOAT:       dataType = ShaderDataType::Float; break;
                case GL_FLOAT_VEC2:  dataType = ShaderDataType::Vec2; break;
                case GL_FLOAT_VEC3:  dataType = ShaderDataType::Vec3; break;
                case GL_FLOAT_VEC4:  dataType = ShaderDataType::Vec4; break;
                case GL_FLOAT_MAT2:  dataType = ShaderDataType::Mat2; break;
                case GL_FLOAT_MAT3:  dataType = ShaderDataType::Mat3; break;
                case GL_FLOAT_MAT4:  dataType = ShaderDataType::Mat4; break;

                case GL_UNSIGNED_INT:      dataType = ShaderDataType::UInt; break;
                case GL_UNSIGNED_INT_VEC2: dataType = ShaderDataType::UVec2; break;
            }

            if (dataType != ShaderDataType::None)
            {
                if (size > 1)
                {
                    for (int j = 0; j < size; j++)
                    {
                        std::string elementName = name + "[" + std::to_string(j) + "]";
                        GLint elementLocation = glGetUniformLocation(m_Program, elementName.c_str());

                        if (elementLocation == -1)
                            continue;

                        UniformInfo info{
                            elementName,
                            dataType,
                            elementLocation
                        };

                        m_ActiveUniforms.push_back(info);
                        m_ActiveUniformsMap.emplace(elementName, info);
                    }
                }
                else
                {
                    UniformInfo info{
                        name,
                        dataType,
                        location
                    };

                    m_ActiveUniforms.push_back(info);
                    m_ActiveUniformsMap.emplace(name, info);
                }
            }

            // -------------------------
            // Samplers
            // -------------------------
            ShaderSamplerType samplerType = ShaderSamplerType::None;

            switch (type)
            {
                case GL_SAMPLER_1D:
                case GL_SAMPLER_1D_SHADOW:
                    samplerType = ShaderSamplerType::Texture1D; break;

                case GL_SAMPLER_2D:
                case GL_SAMPLER_2D_SHADOW:
                    samplerType = ShaderSamplerType::Texture2D; break;

                case GL_SAMPLER_3D:
                    samplerType = ShaderSamplerType::Texture3D; break;

                case GL_SAMPLER_CUBE:
                case GL_SAMPLER_CUBE_SHADOW:
                    samplerType = ShaderSamplerType::TextureCube; break;

                case GL_SAMPLER_1D_ARRAY:
                case GL_SAMPLER_1D_ARRAY_SHADOW:
                    samplerType = ShaderSamplerType::Texture1DArray; break;

                case GL_SAMPLER_2D_ARRAY:
                case GL_SAMPLER_2D_ARRAY_SHADOW:
                    samplerType = ShaderSamplerType::Texture2DArray; break;

                case GL_SAMPLER_CUBE_MAP_ARRAY:
                case GL_SAMPLER_CUBE_MAP_ARRAY_SHADOW:
                    samplerType = ShaderSamplerType::TextureCubeArray; break;

                case GL_SAMPLER_2D_MULTISAMPLE:
                    samplerType = ShaderSamplerType::Texture2DMultisample; break;

                case GL_SAMPLER_2D_MULTISAMPLE_ARRAY:
                    samplerType = ShaderSamplerType::Texture2DMultisampleArray; break;
            }

            if (samplerType != ShaderSamplerType::None)
            {
                if (size > 1)
                {
                    for (int j = 0; j < size; j++)
                    {
                        std::string elementName = name + "[" + std::to_string(j) + "]";
                        GLint elementLocation = glGetUniformLocation(m_Program, elementName.c_str());

                        if (elementLocation == -1)
                            continue;

                        GLint binding = -1;
                        glGetUniformiv(m_Program, elementLocation, &binding);

                        SamplerInfo info{
                            elementName,
                            samplerType,
                            binding
                        };

                        m_ActiveSamplers.push_back(info);
                        m_ActiveSamplersMap.emplace(elementName, info);
                    }
                }
                else
                {
                    GLint binding = -1;
                    glGetUniformiv(m_Program, location, &binding);

                    SamplerInfo info{
                        name,
                        samplerType,
                        binding
                    };

                    m_ActiveSamplers.push_back(info);
                    m_ActiveSamplersMap.emplace(name, info);
                }
            }
        }
    }

    void GLComputeShader::ValidateLocalSize()
    {
        const ComputeLimits& limits = Core::GetEngine().GetRenderer()->GetRendererAPI()->GetComputeLimits();

        for (int i = 0; i < 3; i++)
        {
            if (limits.maxWorkGroupSize[i] > 0 && m_LocalSize[i] > limits.maxWorkGroupSize[i])
            {
                DEBUG_ERROR("Compute shader " + m_FilePath + " declares a local_size of (" +
                    std::to_string(m_LocalSize[0]) + ", " + std::to_string(m_LocalSize[1]) + ", " + std::to_string(m_LocalSize[2]) +
                    "), which exceeds GL_MAX_COMPUTE_WORK_GROUP_SIZE on this driver (" +
                    std::to_string(limits.maxWorkGroupSize[0]) + ", " + std::to_string(limits.maxWorkGroupSize[1]) + ", " + std::to_string(limits.maxWorkGroupSize[2]) + ").");
                break;
            }
        }

        uint64_t invocations = static_cast<uint64_t>(m_LocalSize[0]) * m_LocalSize[1] * m_LocalSize[2];

        if (limits.maxWorkGroupInvocations > 0 && invocations > limits.maxWorkGroupInvocations)
        {
            DEBUG_ERROR("Compute shader " + m_FilePath + " requires " + std::to_string(invocations) +
                " invocations per work group, which exceeds GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS on this driver (" +
                std::to_string(limits.maxWorkGroupInvocations) + ").");
        }
    }

    void GLComputeShader::Bind()
    {
        GLStateCache::BindProgram(m_Program);
    }

    void GLComputeShader::Unbind()
    {
        GLStateCache::BindProgram(0);
    }

    std::vector<UniformInfo> GLComputeShader::GetActiveUniforms()
    {
        return m_ActiveUniforms;
    }

    std::vector<SamplerInfo> GLComputeShader::GetActiveSamplers()
    {
        return m_ActiveSamplers;
    }

    const std::unordered_map<std::string, UniformInfo>& GLComputeShader::GetActiveUniformsMap()
    {
        return m_ActiveUniformsMap;
    }

    const std::unordered_map<std::string, SamplerInfo>& GLComputeShader::GetActiveSamplersMap()
    {
        return m_ActiveSamplersMap;
    }

    int32_t GLComputeShader::GetUniformLocationCached(const std::string &name)
    {
        auto it = m_ActiveUniformsMap.find(name);
        if (it != m_ActiveUniformsMap.end())
            return it->second.location;

        // Array uniforms are cached under "name[0]" ; fall back to that so SetXxx("arr", ...) keeps
        // working the same way glGetUniformLocation("arr") used to (only hit on a miss).
        if (name.empty() || name.back() != ']')
        {
            auto arrayIt = m_ActiveUniformsMap.find(name + "[0]");
            if (arrayIt != m_ActiveUniformsMap.end())
                return arrayIt->second.location;
        }

        // Driver fallback for names the constructor's type switch didn't classify - see the identical
        // GLShader::GetUniformLocationCached for why a miss here is otherwise a silent no-op.
        auto [fallbackIt, inserted] = m_UnmappedLocations.try_emplace(name, -1);
        if (inserted)
            fallbackIt->second = glGetUniformLocation(m_Program, name.c_str());

        return fallbackIt->second;
    }

    void GLComputeShader::SetBool(const std::string &name, bool value)
    {
        glUniform1i(GetUniformLocationCached(name), (int)value);
    }

    void GLComputeShader::SetInt(const std::string &name, int value)
    {
        glUniform1i(GetUniformLocationCached(name), value);
    }

    void GLComputeShader::SetFloat(const std::string &name, float value)
    {
        glUniform1f(GetUniformLocationCached(name), value);
    }

    void GLComputeShader::SetVec2(const std::string &name, const glm::vec2 &value)
    {
        glUniform2fv(GetUniformLocationCached(name), 1, &value[0]);
    }

    void GLComputeShader::SetVec3(const std::string &name, const glm::vec3 &value)
    {
        glUniform3fv(GetUniformLocationCached(name), 1, &value[0]);
    }

    void GLComputeShader::SetVec4(const std::string &name, const glm::vec4 &value)
    {
        glUniform4fv(GetUniformLocationCached(name), 1, &value[0]);
    }

    void GLComputeShader::SetMat2(const std::string &name, const glm::mat2 &mat)
    {
        glUniformMatrix2fv(GetUniformLocationCached(name), 1, GL_FALSE, &mat[0][0]);
    }

    void GLComputeShader::SetMat3(const std::string &name, const glm::mat3 &mat)
    {
        glUniformMatrix3fv(GetUniformLocationCached(name), 1, GL_FALSE, &mat[0][0]);
    }

    void GLComputeShader::SetMat4(const std::string &name, const glm::mat4 &mat)
    {
        glUniformMatrix4fv(GetUniformLocationCached(name), 1, GL_FALSE, &mat[0][0]);
    }

    GLComputeShader::~GLComputeShader()
    {
        glDeleteProgram(m_Program);
    }

}
