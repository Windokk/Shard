#include "gl_shader.hpp"

#include "engine/renderer/rhi/backends/opengl/gl_utils.hpp"
#include "engine/renderer/rhi/shader/glsl_preprocessor.hpp"

#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Rendering{

    void GLShader::CompileErrors(unsigned int shader, const char* type)
    {
        // Stores status of compilation
        GLint hasCompiled;
        // Character array to store error message in
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

    GLShader::GLShader(const Filesystem::Path &vertexPath, const Filesystem::Path &fragmentPath, const Filesystem::Path &geometryPath)
    {
        if (vertexPath.Exists() && !vertexPath.IsDirectory() && fragmentPath.Exists() && !fragmentPath.IsDirectory()) {
		
            m_VertexFilePath = vertexPath.full;
            m_FragmentFilePath = fragmentPath.full;

            // Read vertexFile and fragmentFile, expanding any `#include "..."` directives (see
            // glsl_preprocessor.hpp) before handing the source to the driver - GLSL itself has no
            // include support.
            std::string vertexCode = ResolveGLSLIncludes(vertexPath);
            std::string fragmentCode = ResolveGLSLIncludes(fragmentPath);
    
            // Convert the shader source strings into character arrays
            const char* vertexSource = vertexCode.c_str();
            const char* fragmentSource = fragmentCode.c_str();
    
            // Create Vertex Shader Object and get its reference
            GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
            // Attach Vertex Shader source to the Vertex Shader Object
            glShaderSource(vertexShader, 1, &vertexSource, NULL);
            // Compile the Vertex Shader into machine code
            glCompileShader(vertexShader);
            // Checks if Shader compiled succesfully
            CompileErrors(vertexShader, "VERTEX");
    
            // Create Fragment Shader Object and get its reference
            GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
            // Attach Fragment Shader source to the Fragment Shader Object
            glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
            // Compile the Vertex Shader into machine code
            glCompileShader(fragmentShader);
            // Checks if Shader compiled succesfully
            CompileErrors(fragmentShader, "FRAGMENT");
    
            GLuint geometryShader = 0;
            bool hasGeometry = geometryPath.Exists() && !geometryPath.IsDirectory();

            if (hasGeometry) {
                m_GeometryFilePath = geometryPath.full;
                std::string geometryCode = ResolveGLSLIncludes(geometryPath);
                const char* geometrySource = geometryCode.c_str();

                geometryShader = glCreateShader(GL_GEOMETRY_SHADER);
                glShaderSource(geometryShader, 1, &geometrySource, NULL);
                glCompileShader(geometryShader);
                CompileErrors(geometryShader, "GEOMETRY");
            }

            m_Program = glCreateProgram();
            glAttachShader(m_Program, vertexShader);
            glAttachShader(m_Program, fragmentShader);
            if (hasGeometry)
                glAttachShader(m_Program, geometryShader);

            glLinkProgram(m_Program);
            CompileErrors(m_Program, "PROGRAM");

            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            if (hasGeometry)
                glDeleteShader(geometryShader);

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
    }

    void GLShader::Bind()
    {
        GLStateCache::BindProgram(m_Program);
    }

    void GLShader::Unbind()
    {
        GLStateCache::BindProgram(0);
    }

    std::vector<UniformInfo> GLShader::GetActiveUniforms()
    {
        return m_ActiveUniforms;
    }

    std::vector<SamplerInfo> GLShader::GetActiveSamplers()
    {
        return m_ActiveSamplers;
    }

    const std::unordered_map<std::string, UniformInfo>& GLShader::GetActiveUniformsMap()
    {
        return m_ActiveUniformsMap;
    }

    const std::unordered_map<std::string, SamplerInfo>& GLShader::GetActiveSamplersMap()
    {
        return m_ActiveSamplersMap;
    }

    int32_t GLShader::GetUniformLocationCached(const std::string &name)
    {
        auto it = m_ActiveUniformsMap.find(name);
        if (it != m_ActiveUniformsMap.end())
            return it->second.location;

        // Array uniforms are cached under "name[0]" ; fall back to that so SetXxx("arr", ...)
        // keeps working the same way glGetUniformLocation("arr") used to (only hit on a miss).
        if (name.empty() || name.back() != ']')
        {
            auto arrayIt = m_ActiveUniformsMap.find(name + "[0]");
            if (arrayIt != m_ActiveUniformsMap.end())
                return arrayIt->second.location;
        }

        // Last resort : ask the driver once and remember the answer, -1 included. m_ActiveUniformsMap
        // only holds the GL types the constructor's switch knows how to classify, so a uniform of any
        // other type would otherwise resolve to -1 forever - and glUniform*(-1, ...) is defined to do
        // nothing silently, which turns the whole SetXxx into a no-op with no error anywhere. That's
        // how lit.frag's two uvec2 uniforms (ssaoTextureHandle, clusterGridSizeXY) sat at zero. Still
        // one round-trip per name per program, not per call, so the hot path is unchanged.
        auto [fallbackIt, inserted] = m_UnmappedLocations.try_emplace(name, -1);
        if (inserted)
            fallbackIt->second = glGetUniformLocation(m_Program, name.c_str());

        return fallbackIt->second;
    }

    void GLShader::SetBool(const std::string &name, bool value)
    {
        glUniform1i(GetUniformLocationCached(name), (int)value);
    }

    void GLShader::SetInt(const std::string &name, int value)
    {
        glUniform1i(GetUniformLocationCached(name), value);
    }

    void GLShader::SetFloat(const std::string &name, float value)
    {
        glUniform1f(GetUniformLocationCached(name), value);
    }

    void GLShader::SetVec2(const std::string &name, const glm::vec2 &value)
    {
        glUniform2fv(GetUniformLocationCached(name), 1, &value[0]);
    }

    void GLShader::SetVec3(const std::string &name, const glm::vec3 &value)
    {
        glUniform3fv(GetUniformLocationCached(name), 1, &value[0]);
    }

    void GLShader::SetVec4(const std::string &name, const glm::vec4 &value)
    {
        glUniform4fv(GetUniformLocationCached(name), 1, &value[0]);
    }

    void GLShader::SetMat2(const std::string &name, const glm::mat2 &mat)
    {
        glUniformMatrix2fv(GetUniformLocationCached(name), 1, GL_FALSE, &mat[0][0]);
    }

    void GLShader::SetMat3(const std::string &name, const glm::mat3 &mat)
    {
        glUniformMatrix3fv(GetUniformLocationCached(name), 1, GL_FALSE, &mat[0][0]);
    }

    void GLShader::SetMat4(const std::string &name, const glm::mat4 &mat)
    {
        glUniformMatrix4fv(GetUniformLocationCached(name), 1, GL_FALSE, &mat[0][0]);
    }

    void GLShader::SetUVec2(const std::string &name, uint32_t x, uint32_t y)
    {
        glUniform2ui(GetUniformLocationCached(name), x, y);
    }

    GLShader::~GLShader()
    {
        glDeleteProgram(m_Program);
    }

}