#include "vk_shader.hpp"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

#include "vk_buffer.hpp"

#include "engine/renderer/rhi/shader_compiler/shader_compiler.hpp"
#include "engine/renderer/rhi/render_thread.hpp"

namespace Shard::Engine::Rendering {

    using namespace VK;

    namespace {

        std::atomic<uint32_t> g_NextProgramId{1};

        std::mutex g_LayoutMutex;
        std::unordered_map<std::string, VkDescriptorSetLayout> g_Layouts;

        VkShaderStageFlags StageFlags(uint32_t mask)
        {
            VkShaderStageFlags flags = 0;
            if (mask & ShaderStageBit(ShaderStageKind::Vertex))   flags |= VK_SHADER_STAGE_VERTEX_BIT;
            if (mask & ShaderStageBit(ShaderStageKind::Fragment)) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
            if (mask & ShaderStageBit(ShaderStageKind::Geometry)) flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
            if (mask & ShaderStageBit(ShaderStageKind::Compute))  flags |= VK_SHADER_STAGE_COMPUTE_BIT;
            return flags;
        }

        VkDescriptorType DescriptorTypeOf(BindingKind kind)
        {
            switch (kind)
            {
                case BindingKind::UniformBuffer:    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                case BindingKind::StorageBuffer:    return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                case BindingKind::StorageImage:     return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                default:                            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            }
        }

        /// A layout with these bindings : shared by every program that declares the same, they are made once
        VkDescriptorSetLayout FindOrCreateSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings)
        {
            std::string key;
            for (const VkDescriptorSetLayoutBinding& b : bindings)
                key += std::to_string(b.binding) + ":" + std::to_string(b.descriptorType) + ":" + std::to_string(b.descriptorCount) + ":" + std::to_string(b.stageFlags) + ";";

            std::lock_guard<std::mutex> lock(g_LayoutMutex);
            auto it = g_Layouts.find(key);
            if (it != g_Layouts.end())
                return it->second;

            VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            info.bindingCount = static_cast<uint32_t>(bindings.size());
            info.pBindings = bindings.data();

            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            VK_CHECK(vkCreateDescriptorSetLayout(Context::Get().GetDevice(), &info, nullptr, &layout));
            g_Layouts[key] = layout;
            return layout;
        }

        std::filesystem::path CachePathFor(const std::filesystem::path& stageFile, const std::vector<std::string>& defines)
        {
            // A permutation is a binary of its own : its defines are part of the name
            std::string suffix;
            for (const std::string& define : defines)
                suffix += "." + define;
            for (char& c : suffix)
            {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_')
                    c = '-';
            }

            const std::filesystem::path directory = stageFile.parent_path();
            const std::string name = stageFile.stem().string();

            // The shader root is the nearest directory called "shaders" : the binaries sit in "shaders_spv" next to it, same layout
            for (std::filesystem::path root = directory; !root.empty() && root.has_parent_path() && root != root.parent_path(); root = root.parent_path())
            {
                if (root.filename() == "shaders")
                {
                    std::error_code ec;
                    return root.parent_path() / "shaders_spv" / std::filesystem::relative(directory, root, ec) / (name + suffix + ".spvpack");
                }
            }

            return directory / "shaders_spv" / (name + suffix + ".spvpack");
        }
    }

    void ClearShaderLayoutCache()
    {
        if (!Context::Exists())
            return;

        std::lock_guard<std::mutex> lock(g_LayoutMutex);
        for (auto& [key, layout] : g_Layouts)
            vkDestroyDescriptorSetLayout(Context::Get().GetDevice(), layout, nullptr);
        g_Layouts.clear();
    }

    // ---- VKProgram -----------------------------------------------------------------------------------------------------------------

    VKProgram::~VKProgram()
    {
        Release();
    }

    void VKProgram::Release()
    {
        if (!Context::Exists())
            return;

        VkDevice device = Context::Get().GetDevice();
        std::array<VkShaderModule, static_cast<size_t>(ShaderStageKind::Count)> modules = m_Modules;
        VkPipelineLayout layout = m_PipelineLayout;
        VkDescriptorSet uniformSet = m_UniformSet;

        Context::Get().Defer([device, modules, layout] {
            for (VkShaderModule module : modules)
            {
                if (module)
                    vkDestroyShaderModule(device, module, nullptr);
            }
            if (layout)
                vkDestroyPipelineLayout(device, layout, nullptr);
        });
        Context::Get().FreePersistentSet(uniformSet);

        m_Modules = {};
        m_PipelineLayout = VK_NULL_HANDLE;
        m_UniformSet = VK_NULL_HANDLE;
        m_Valid = false;
    }

    bool VKProgram::Load(const std::vector<std::pair<ShaderStageKind, std::string>>& files, const std::string& name, const std::vector<std::string>& defines)
    {
        m_Name = name;

        ShaderCompiler::Options options;
        for (const std::string& define : defines)
        {
            const size_t equals = define.find('=');
            options.defines.emplace_back(define.substr(0, equals), equals == std::string::npos ? "1" : define.substr(equals + 1));
        }

        std::vector<ShaderCompiler::StageSource> stages;
        for (const auto& [kind, path] : files)
        {
            ShaderCompiler::StageSource stage;
            stage.kind = kind;
            stage.label = path;
            stage.language = ShaderCompiler::LanguageOfFile(path);
            stage.file = path;
            std::string error;
            stage.source = ShaderCompiler::ResolveIncludes(path, &error);
            if (!error.empty())
            {
                DEBUG_ERROR("Cannot read the shader ", path, " : ", error);
                return false;
            }
            stages.push_back(std::move(stage));
        }

        const uint64_t hash = ShaderCompiler::HashSources(stages, options);
        const std::filesystem::path cachePath = CachePathFor(files.front().second, defines);

        ShaderBinary binary;
        bool fromCache = false;

        {
            std::ifstream file(cachePath, std::ios::binary);
            if (file)
            {
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                if (DeserializeShaderBinary(bytes.data(), bytes.size(), binary) && binary.sourceHash == hash)
                    fromCache = true;
            }
        }

        if (!fromCache)
        {
            ShaderCompiler::Result result = ShaderCompiler::CompileProgram(stages, options);
            if (!result.ok)
            {
                DEBUG_ERROR("Could not compile the shader '", name, "' for Vulkan :\n", result.log);
                return false;
            }

            if (!result.log.empty())
                DEBUG_LOG("Shader '", name, "' : ", result.log);

            binary = std::move(result.binary);

            std::error_code ec;
            std::filesystem::create_directories(cachePath.parent_path(), ec);
            const std::vector<uint8_t> bytes = SerializeShaderBinary(binary);
            std::ofstream file(cachePath, std::ios::binary | std::ios::trunc);
            if (file)
                file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        // Replace what the program was (a reload) only once the new one is known good
        VKProgram fresh;
        fresh.m_Name = name;
        fresh.m_Binary = std::move(binary);
        if (!fresh.Build())
            return false;

        Release();
        m_Binary = std::move(fresh.m_Binary);
        m_Modules = fresh.m_Modules;
        m_SetLayouts = fresh.m_SetLayouts;
        m_PipelineLayout = fresh.m_PipelineLayout;
        m_UniformSet = fresh.m_UniformSet;
        m_Shadow = std::move(fresh.m_Shadow);
        m_Uniforms = std::move(fresh.m_Uniforms);
        m_UniformInfos = std::move(fresh.m_UniformInfos);
        m_SamplerInfos = std::move(fresh.m_SamplerInfos);
        m_SamplerUnits.clear();
        m_SamplerBindingByName = std::move(fresh.m_SamplerBindingByName);
        m_Dirty = true;
        m_UploadedFrame = ~0ull;
        m_Valid = true;
        m_Version++;
        if (m_Id == 0)
            m_Id = g_NextProgramId++;

        // `fresh` gave everything away
        fresh.m_Modules = {};
        fresh.m_PipelineLayout = VK_NULL_HANDLE;
        fresh.m_UniformSet = VK_NULL_HANDLE;
        fresh.m_Valid = false;
        return true;
    }

    bool VKProgram::Build()
    {
        VkDevice device = Context::Get().GetDevice();

        for (size_t s = 0; s < m_Binary.spirv.size(); s++)
        {
            const std::vector<uint32_t>& words = m_Binary.spirv[s];
            if (words.empty())
                continue;

            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.codeSize = words.size() * sizeof(uint32_t);
            info.pCode = words.data();
            if (VK_CHECK(vkCreateShaderModule(device, &info, nullptr, &m_Modules[s])) != VK_SUCCESS)
                return false;
            SetObjectName(VK_OBJECT_TYPE_SHADER_MODULE, m_Modules[s], m_Name + " " + ShaderStageName(static_cast<ShaderStageKind>(s)));
        }

        if (!BuildLayouts())
            return false;

        IndexReflection();
        m_Shadow.assign(m_Binary.reflection.uniformBlockSize, 0);
        return true;
    }

    bool VKProgram::BuildLayouts()
    {
        Context& context = Context::Get();
        std::vector<VkDescriptorSetLayoutBinding> bindings[BindingSets::Count];

        for (const ReflectedBinding& binding : m_Binary.reflection.bindings)
        {
            if (binding.set >= BindingSets::Count || binding.set == BindingSets::Bindless)
                continue;

            VkDescriptorSetLayoutBinding entry{};
            entry.binding = binding.binding;
            entry.descriptorType = DescriptorTypeOf(binding.kind);
            entry.descriptorCount = std::max(binding.count, 1u);
            entry.stageFlags = StageFlags(binding.stages);
            bindings[binding.set].push_back(entry);
        }

        for (uint32_t set = 0; set < BindingSets::Count; set++)
        {
            if (set == BindingSets::Bindless)
                m_SetLayouts[set] = context.GetBindlessLayout();
            else
                m_SetLayouts[set] = FindOrCreateSetLayout(bindings[set]);

            if (m_SetLayouts[set] == VK_NULL_HANDLE)
                return false;
        }

        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = BindingSets::Count;
        layoutInfo.pSetLayouts = m_SetLayouts.data();
        if (VK_CHECK(vkCreatePipelineLayout(context.GetDevice(), &layoutInfo, nullptr, &m_PipelineLayout)) != VK_SUCCESS)
            return false;

        // The uniform block : one persistent set that looks at the whole ring, the draws only move its offset
        if (m_Binary.reflection.uniformBlockSize > 0)
        {
            m_UniformSet = context.AllocatePersistentSet(m_SetLayouts[BindingSets::Uniforms]);
            if (m_UniformSet == VK_NULL_HANDLE)
                return false;

            VkDescriptorBufferInfo bufferInfo{context.GetUniformRing(), 0, std::max<uint32_t>(m_Binary.reflection.uniformBlockSize, 16)};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = m_UniformSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            write.pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(context.GetDevice(), 1, &write, 0, nullptr);
        }

        return true;
    }

    void VKProgram::IndexReflection()
    {
        const ShaderReflection& reflection = m_Binary.reflection;

        m_Uniforms.clear();
        m_UniformInfos.clear();
        m_SamplerInfos.clear();
        m_SamplerBindingByName.clear();

        int location = 0;
        for (const ReflectedUniform& uniform : reflection.uniforms)
        {
            m_Uniforms[uniform.name] = uniform;

            // The array by its name is its first element, as `glGetUniformLocation("array")` is
            const size_t first = uniform.name.find("[0]");
            if (first != std::string::npos && uniform.name.find('[', first + 3) == std::string::npos && first + 3 == uniform.name.size())
                m_Uniforms.emplace(uniform.name.substr(0, first), uniform);

            if (uniform.type != ShaderDataType::None)
                m_UniformInfos.emplace(uniform.name, UniformInfo{uniform.name, uniform.type, location});
            location++;
        }

        for (const ReflectedBinding& binding : reflection.bindings)
        {
            if (binding.kind != BindingKind::SampledImage)
                continue;

            m_SamplerBindingByName[binding.name] = binding.binding;

            if (binding.count > 1)
            {
                for (uint32_t j = 0; j < binding.count; j++)
                {
                    const std::string element = binding.name + "[" + std::to_string(j) + "]";
                    m_SamplerInfos.emplace(element, SamplerInfo{element, binding.samplerType, static_cast<int32_t>(binding.binding + j)});
                }
            }
            else
                m_SamplerInfos.emplace(binding.name, SamplerInfo{binding.name, binding.samplerType, static_cast<int32_t>(binding.binding)});
        }
    }

    bool VKProgram::HasUniform(const std::string& name) const
    {
        return m_Uniforms.find(name) != m_Uniforms.end();
    }

    bool VKProgram::WriteUniform(const std::string& name, const void* data, uint32_t size, uint32_t, uint32_t)
    {
        auto it = m_Uniforms.find(name);
        if (it == m_Uniforms.end())
        {
            // An array named without its index is its first element
            if (name.empty() || name.back() == ']')
                return false;
            it = m_Uniforms.find(name + "[0]");
            if (it == m_Uniforms.end())
                return false;
        }

        const ReflectedUniform& uniform = it->second;
        const uint32_t bytes = std::min(size, uniform.size);
        if (uniform.offset + bytes > m_Shadow.size())
            return false;

        uint8_t* destination = m_Shadow.data() + uniform.offset;
        if (std::memcmp(destination, data, bytes) != 0)
        {
            std::memcpy(destination, data, bytes);
            m_Dirty = true;
        }
        return true;
    }

    bool VKProgram::SetSamplerUnit(const std::string& name, uint32_t unit)
    {
        auto it = m_SamplerBindingByName.find(name);
        if (it == m_SamplerBindingByName.end())
            return false;

        m_SamplerUnits[it->second] = unit;
        return true;
    }

    uint32_t VKProgram::GetSamplerUnit(const ReflectedBinding& binding, uint32_t element) const
    {
        auto it = m_SamplerUnits.find(binding.binding);
        return (it != m_SamplerUnits.end() ? it->second : binding.binding) + element;
    }

    bool VKProgram::FlushUniforms(uint32_t& dynamicOffset)
    {
        if (m_Shadow.empty() || m_UniformSet == VK_NULL_HANDLE)
            return false;

        Context& context = Context::Get();

        // Already in the ring of this recording, and unchanged since
        if (!m_Dirty && m_UploadedFrame == context.FrameIndex())
        {
            dynamicOffset = m_UploadedOffset;
            return true;
        }

        uint32_t offset = 0;
        void* mapped = nullptr;
        if (!context.AllocUniform(static_cast<uint32_t>(m_Shadow.size()), offset, mapped))
        {
            DEBUG_ERROR("The uniform ring of the frame is full (the shader '", m_Name, "') : this draw uses the last uniforms that fit.");
            dynamicOffset = m_UploadedOffset;
            return true;
        }

        std::memcpy(mapped, m_Shadow.data(), m_Shadow.size());
        m_UploadedOffset = offset;
        m_UploadedFrame = context.FrameIndex();
        m_Dirty = false;
        dynamicOffset = offset;
        return true;
    }

    // ---- VKShader ------------------------------------------------------------------------------------------------------------------

    namespace {

        std::string NativePath(const Filesystem::Path& path)
        {
            return path.GetNativePath();
        }

        std::vector<UniformInfo> ListUniforms(const std::unordered_map<std::string, UniformInfo>& map)
        {
            std::vector<UniformInfo> out;
            out.reserve(map.size());
            for (const auto& [name, info] : map)
                out.push_back(info);
            std::sort(out.begin(), out.end(), [](const UniformInfo& a, const UniformInfo& b) { return a.location < b.location; });
            return out;
        }

        std::vector<SamplerInfo> ListSamplers(const std::unordered_map<std::string, SamplerInfo>& map)
        {
            std::vector<SamplerInfo> out;
            out.reserve(map.size());
            for (const auto& [name, info] : map)
                out.push_back(info);
            std::sort(out.begin(), out.end(), [](const SamplerInfo& a, const SamplerInfo& b) { return a.binding < b.binding; });
            return out;
        }

        template<typename Program>
        void WriteMat3(Program& program, const std::string& name, const glm::mat3& mat)
        {
            // std140 : each column of a mat3 takes a vec4
            float padded[12] = {};
            for (int c = 0; c < 3; c++)
                for (int r = 0; r < 3; r++)
                    padded[c * 4 + r] = mat[c][r];
            program.WriteUniform(name, padded, sizeof(padded));
        }

        template<typename Program>
        void WriteMat2(Program& program, const std::string& name, const glm::mat2& mat)
        {
            float padded[8] = {};
            for (int c = 0; c < 2; c++)
                for (int r = 0; r < 2; r++)
                    padded[c * 4 + r] = mat[c][r];
            program.WriteUniform(name, padded, sizeof(padded));
        }
    }

    VKShader::VKShader(const Filesystem::Path& vertexPath, const Filesystem::Path& fragmentPath, const Filesystem::Path& geometryPath, const std::vector<std::string>& defines)
    {
        m_Defines = defines;

        if (!(vertexPath.Exists() && !vertexPath.IsDirectory() && fragmentPath.Exists() && !fragmentPath.IsDirectory()))
        {
            DEBUG_ERROR("Shader files not found : ", vertexPath.full, " / ", fragmentPath.full);
            return;
        }

        m_VertexFilePath = vertexPath.full;
        m_FragmentFilePath = fragmentPath.full;

        std::vector<std::pair<ShaderStageKind, std::string>> files = {
            {ShaderStageKind::Vertex, NativePath(vertexPath)},
            {ShaderStageKind::Fragment, NativePath(fragmentPath)},
        };

        if (geometryPath.Exists() && !geometryPath.IsDirectory())
        {
            m_GeometryFilePath = geometryPath.full;
            files.push_back({ShaderStageKind::Geometry, NativePath(geometryPath)});
        }

        m_Program.Load(files, vertexPath.GetFilename(false), defines);
    }

    std::vector<UniformInfo> VKShader::GetActiveUniforms() { return ListUniforms(m_Program.GetUniformMap()); }
    std::vector<SamplerInfo> VKShader::GetActiveSamplers() { return ListSamplers(m_Program.GetSamplerMap()); }

    void VKShader::SetBool(const std::string& name, bool value) { const uint32_t v = value ? 1u : 0u; m_Program.WriteUniform(name, &v, sizeof(v)); }

    void VKShader::SetInt(const std::string& name, int value)
    {
        // `glUniform1i` on a sampler says which texture unit it reads
        if (m_Program.SetSamplerUnit(name, static_cast<uint32_t>(value)))
            return;
        m_Program.WriteUniform(name, &value, sizeof(value));
    }

    void VKShader::SetFloat(const std::string& name, float value) { m_Program.WriteUniform(name, &value, sizeof(value)); }
    void VKShader::SetVec2(const std::string& name, const glm::vec2& value) { m_Program.WriteUniform(name, &value, sizeof(value)); }
    void VKShader::SetVec3(const std::string& name, const glm::vec3& value) { m_Program.WriteUniform(name, &value, sizeof(value)); }
    void VKShader::SetVec4(const std::string& name, const glm::vec4& value) { m_Program.WriteUniform(name, &value, sizeof(value)); }
    void VKShader::SetMat2(const std::string& name, const glm::mat2& mat) { WriteMat2(m_Program, name, mat); }
    void VKShader::SetMat3(const std::string& name, const glm::mat3& mat) { WriteMat3(m_Program, name, mat); }
    void VKShader::SetMat4(const std::string& name, const glm::mat4& mat) { m_Program.WriteUniform(name, &mat, sizeof(mat)); }

    void VKShader::SetUVec2(const std::string& name, uint32_t x, uint32_t y)
    {
        const uint32_t value[2] = {x, y};
        m_Program.WriteUniform(name, value, sizeof(value));
    }

    bool VKShader::Reload()
    {
        if (m_VertexFilePath.empty() || m_FragmentFilePath.empty())
            return false;

        std::vector<std::pair<ShaderStageKind, std::string>> files = {
            {ShaderStageKind::Vertex, NativePath(Filesystem::Path(m_VertexFilePath))},
            {ShaderStageKind::Fragment, NativePath(Filesystem::Path(m_FragmentFilePath))},
        };
        if (!m_GeometryFilePath.empty())
            files.push_back({ShaderStageKind::Geometry, NativePath(Filesystem::Path(m_GeometryFilePath))});

        // The pipelines that were built from the old modules are recognised by the version they were built for
        const bool ok = m_Program.Load(files, Filesystem::Path(m_VertexFilePath).GetFilename(false), m_Defines);
        if (ok)
            m_DepthPrepassSupport = -1;
        return ok;
    }

    // ---- VKComputeShader -----------------------------------------------------------------------------------------------------------

    VKComputeShader::VKComputeShader(const Filesystem::Path& path)
    {
        if (!path.Exists() || path.IsDirectory())
        {
            DEBUG_ERROR("Compute shader file not found : ", path.full);
            return;
        }

        m_FilePath = path.full;
        m_Program.Load({{ShaderStageKind::Compute, NativePath(path)}}, path.GetFilename(false));
    }

    std::vector<UniformInfo> VKComputeShader::GetActiveUniforms() { return ListUniforms(m_Program.GetUniformMap()); }
    std::vector<SamplerInfo> VKComputeShader::GetActiveSamplers() { return ListSamplers(m_Program.GetSamplerMap()); }

    void VKComputeShader::SetBool(const std::string& name, bool value) { const uint32_t v = value ? 1u : 0u; m_Program.WriteUniform(name, &v, sizeof(v)); }

    void VKComputeShader::SetInt(const std::string& name, int value)
    {
        if (m_Program.SetSamplerUnit(name, static_cast<uint32_t>(value)))
            return;
        m_Program.WriteUniform(name, &value, sizeof(value));
    }

    void VKComputeShader::SetFloat(const std::string& name, float value) { m_Program.WriteUniform(name, &value, sizeof(value)); }
    void VKComputeShader::SetVec2(const std::string& name, const glm::vec2& value) { m_Program.WriteUniform(name, &value, sizeof(value)); }
    void VKComputeShader::SetVec3(const std::string& name, const glm::vec3& value) { m_Program.WriteUniform(name, &value, sizeof(value)); }
    void VKComputeShader::SetVec4(const std::string& name, const glm::vec4& value) { m_Program.WriteUniform(name, &value, sizeof(value)); }
    void VKComputeShader::SetMat2(const std::string& name, const glm::mat2& mat) { WriteMat2(m_Program, name, mat); }
    void VKComputeShader::SetMat3(const std::string& name, const glm::mat3& mat) { WriteMat3(m_Program, name, mat); }
    void VKComputeShader::SetMat4(const std::string& name, const glm::mat4& mat) { m_Program.WriteUniform(name, &mat, sizeof(mat)); }

    bool VKComputeShader::Reload()
    {
        if (m_FilePath.empty())
            return false;
        return m_Program.Load({{ShaderStageKind::Compute, NativePath(Filesystem::Path(m_FilePath))}}, Filesystem::Path(m_FilePath).GetFilename(false));
    }

}
