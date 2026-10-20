#include "shader_compiler.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_set>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

extern "C" {
#include "spirv_reflect.h"
}

namespace Shard::Engine::Rendering::ShaderCompiler {

    namespace {

        constexpr const char* kUniformBlockName = "ShardUniforms";
        constexpr const char* kBindlessArray = "uShardBindless2D";

        EShLanguage ToGlslang(ShaderStageKind kind)
        {
            switch (kind)
            {
                case ShaderStageKind::Vertex:   return EShLangVertex;
                case ShaderStageKind::Fragment: return EShLangFragment;
                case ShaderStageKind::Geometry: return EShLangGeometry;
                default:                        return EShLangCompute;
            }
        }

        void EnsureGlslangInitialised()
        {
            static std::once_flag once;
            std::call_once(once, [] { glslang::InitializeProcess(); });
        }

        const char* SlangStageName(ShaderStageKind kind)
        {
            switch (kind)
            {
                case ShaderStageKind::Vertex:   return "vertex";
                case ShaderStageKind::Fragment: return "fragment";
                case ShaderStageKind::Geometry: return "geometry";
                default:                        return "compute";
            }
        }

        std::string Quoted(const std::filesystem::path& path)
        {
            return "\"" + path.string() + "\"";
        }

        /// One stage of a Slang file, compiled by slangc to SPIR-V (a process per stage : each stage is a module of its own)
        bool CompileSlangStage(const StageSource& stage, const Options& options, std::vector<uint32_t>& words, std::string& log)
        {
            const std::filesystem::path slangc = FindSlangCompiler();
            if (slangc.empty())
            {
                log += "slangc was not found : it is part of the Vulkan SDK (set SHARD_SLANGC to use another), or compile the shader "
                       "ahead of time with shaderc on a machine that has it\n";
                return false;
            }

            static std::atomic<uint32_t> counter{0};
            // The thread id and the counter are only unique in this process : two processes (ctest -j) start with the same ones
            static const uint64_t process = (uint64_t(std::random_device{}()) << 32) | std::random_device{}();
            std::error_code ec;
            const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
            const std::string unique = "shard_slang_" + std::to_string(process) + "_" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + "_" + std::to_string(counter++);
            const std::filesystem::path output = temp / (unique + ".spv");
            const std::filesystem::path diagnostics = temp / (unique + ".log");

            const std::string entry = DefaultEntryPoint(SourceLanguage::Slang, stage.kind);

            // The matrices the engine uploads are glm's : column major
            std::string command = Quoted(slangc) + " " + Quoted(stage.file) + " -target spirv -entry " + entry + " -stage " + SlangStageName(stage.kind) +
                                  " -matrix-layout-column-major -o " + Quoted(output);
            for (const auto& [name, value] : options.defines)
                command += " -D" + name + "=" + value;
            command += " > " + Quoted(diagnostics) + " 2>&1";

            // cmd.exe strips the first and the last quote of a command line that starts with one
#ifdef _WIN32
            command = "\"" + command + "\"";
#endif
            const int status = std::system(command.c_str());

            {
                std::ifstream text(diagnostics, std::ios::binary);
                std::string diagnosticsText((std::istreambuf_iterator<char>(text)), std::istreambuf_iterator<char>());
                if (!diagnosticsText.empty())
                    log += diagnosticsText + "\n";
            }
            std::filesystem::remove(diagnostics, ec);

            std::ifstream spirv(output, std::ios::binary);
            std::vector<char> bytes((std::istreambuf_iterator<char>(spirv)), std::istreambuf_iterator<char>());
            spirv.close();
            std::filesystem::remove(output, ec);

            if (status != 0 || bytes.size() < 4 || bytes.size() % 4 != 0)
            {
                log += "slangc failed on the " + std::string(ShaderStageName(stage.kind)) + " stage (" + entry + ") of " + stage.label + "\n";
                return false;
            }

            words.resize(bytes.size() / 4);
            std::memcpy(words.data(), bytes.data(), bytes.size());
            return true;
        }

        // ---- Source preparation ------------------------------------------------------------------------------------------------

        [[maybe_unused]] std::string ReplaceAll(std::string text, const std::string& from, const std::string& to)
        {
            size_t at = 0;
            while ((at = text.find(from, at)) != std::string::npos)
            {
                text.replace(at, from.size(), to);
                at += to.size();
            }
            return text;
        }

        bool IsIdentifierChar(char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        /// `sampler2D(handle)` : the constructor that turns a bindless handle into a sampler. A declaration is `sampler2D name`, the constructor
        /// is the type name followed by a parenthesis.
        bool UsesBindlessConstructor(const std::string& source)
        {
            size_t at = 0;
            const std::string token = "sampler2D(";
            while ((at = source.find(token, at)) != std::string::npos)
            {
                if (at == 0 || !IsIdentifierChar(source[at - 1]))
                    return true;
                at += token.size();
            }
            return false;
        }

        std::string RewriteBindlessConstructors(const std::string& source)
        {
            std::string out;
            out.reserve(source.size() + 64);

            const std::string token = "sampler2D(";
            size_t at = 0;
            while (true)
            {
                const size_t found = source.find(token, at);
                if (found == std::string::npos)
                {
                    out.append(source, at, std::string::npos);
                    break;
                }

                out.append(source, at, found - at);
                if (found == 0 || !IsIdentifierChar(source[found - 1]))
                    out += "ShardBindlessSampler2D(";
                else
                    out += token;
                at = found + token.size();
            }
            return out;
        }

        /// What the stage's own text has to say before glslang reads it : the version, the extension that does not exist for Vulkan
        std::string PrepareStageSource(const std::string& source)
        {
            std::istringstream in(source);
            std::ostringstream out;
            std::string line;
            bool versionSeen = false;

            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                size_t first = line.find_first_not_of(" \t");
                if (first != std::string::npos && line.compare(first, 8, "#version") == 0)
                {
                    // The Vulkan dialect starts at 450 : what a 430 shader says it needs, a 460 one has
                    out << "#version 460 core\n";
                    versionSeen = true;
                    continue;
                }

                if (first != std::string::npos && line.compare(first, 10, "#extension") == 0 && line.find("GL_ARB_bindless_texture") != std::string::npos)
                {
                    out << "\n";     // the bindless array replaces it (see the preamble) ; the line stays, so that the numbers of the log do
                    continue;
                }

                out << line << "\n";
            }

            std::string text = out.str();
            if (!versionSeen)
                text = "#version 460 core\n" + text;
            return RewriteBindlessConstructors(text);
        }

        std::string BuildPreamble(const Options& options, bool bindless)
        {
            std::ostringstream preamble;
            preamble << "#extension GL_EXT_nonuniform_qualifier : require\n";
            preamble << "#define SHARD_VULKAN 1\n";
            for (const auto& [name, value] : options.defines)
                preamble << "#define " << name << " " << value << "\n";

            if (bindless)
            {
                preamble << "layout(set = " << BindingSets::Bindless << ", binding = 0) uniform sampler2D " << kBindlessArray << "[];\n";
                // A bindless handle is the pair of uints the CPU wrote : the first is the texture's index in the array
                preamble << "#define ShardBindlessSampler2D(handle) " << kBindlessArray << "[nonuniformEXT((handle).x)]\n";
            }

            return preamble.str();
        }

        std::set<std::string> FindBoolUniforms(const std::string& source)
        {
            std::set<std::string> names;
            static const std::regex pattern(R"(\buniform\s+(?:highp\s+|mediump\s+|lowp\s+)?bool\s+([A-Za-z_][A-Za-z0-9_]*))");
            for (auto it = std::sregex_iterator(source.begin(), source.end(), pattern); it != std::sregex_iterator(); ++it)
                names.insert((*it)[1].str());
            return names;
        }

        // ---- Reflection --------------------------------------------------------------------------------------------------------

        ShaderSamplerType SamplerTypeOf(const SpvReflectImageTraits& image)
        {
            const bool shadow = image.depth == 1;
            const bool arrayed = image.arrayed != 0;
            const bool multisampled = image.ms != 0;

            switch (image.dim)
            {
                case SpvDim1D:
                    return shadow ? (arrayed ? ShaderSamplerType::Texture1DArrayShadow : ShaderSamplerType::Texture1DShadow)
                                  : (arrayed ? ShaderSamplerType::Texture1DArray : ShaderSamplerType::Texture1D);
                case SpvDim2D:
                    if (multisampled)
                        return arrayed ? ShaderSamplerType::Texture2DMultisampleArray : ShaderSamplerType::Texture2DMultisample;
                    return shadow ? (arrayed ? ShaderSamplerType::Texture2DArrayShadow : ShaderSamplerType::Texture2DShadow)
                                  : (arrayed ? ShaderSamplerType::Texture2DArray : ShaderSamplerType::Texture2D);
                case SpvDim3D:
                    return ShaderSamplerType::Texture3D;
                case SpvDimCube:
                    return shadow ? (arrayed ? ShaderSamplerType::TextureCubeArrayShadow : ShaderSamplerType::TextureCubeShadow)
                                  : (arrayed ? ShaderSamplerType::TextureCubeArray : ShaderSamplerType::TextureCube);
                default:
                    return ShaderSamplerType::Texture2D;
            }
        }

        ShaderDataType DataTypeOf(const SpvReflectTypeDescription* type, const SpvReflectNumericTraits& numeric, bool isBool)
        {
            const uint32_t flags = type ? type->type_flags : 0;
            const bool isFloat = (flags & SPV_REFLECT_TYPE_FLAG_FLOAT) != 0;
            const bool isInt = (flags & SPV_REFLECT_TYPE_FLAG_INT) != 0;
            const bool isUnsigned = isInt && numeric.scalar.signedness == 0;

            if (flags & SPV_REFLECT_TYPE_FLAG_MATRIX)
            {
                if (!isFloat || numeric.matrix.column_count != numeric.matrix.row_count)
                    return ShaderDataType::None;
                switch (numeric.matrix.column_count)
                {
                    case 2: return ShaderDataType::Mat2;
                    case 3: return ShaderDataType::Mat3;
                    case 4: return ShaderDataType::Mat4;
                    default: return ShaderDataType::None;
                }
            }

            if (flags & SPV_REFLECT_TYPE_FLAG_VECTOR)
            {
                const uint32_t components = numeric.vector.component_count;
                if (isFloat)
                {
                    switch (components)
                    {
                        case 2: return ShaderDataType::Vec2;
                        case 3: return ShaderDataType::Vec3;
                        case 4: return ShaderDataType::Vec4;
                        default: return ShaderDataType::None;
                    }
                }
                if (isUnsigned && components == 2)
                    return ShaderDataType::UVec2;
                return ShaderDataType::None;
            }

            if (isBool || (flags & SPV_REFLECT_TYPE_FLAG_BOOL))
                return ShaderDataType::Bool;
            if (isFloat)
                return ShaderDataType::Float;
            if (isInt)
                return isUnsigned ? ShaderDataType::UInt : ShaderDataType::Int;
            return ShaderDataType::None;
        }

        uint32_t LeafSize(const SpvReflectBlockVariable& variable, ShaderDataType type)
        {
            const uint32_t flags = variable.type_description ? variable.type_description->type_flags : 0;

            if (flags & SPV_REFLECT_TYPE_FLAG_MATRIX)
            {
                // The columns sit at the stride of the layout (16 bytes for a mat3 in std140)
                const uint32_t stride = variable.numeric.matrix.stride ? variable.numeric.matrix.stride : 16;
                return stride * variable.numeric.matrix.column_count;
            }

            if (flags & SPV_REFLECT_TYPE_FLAG_VECTOR)
                return variable.numeric.vector.component_count * std::max<uint32_t>(variable.numeric.scalar.width / 8, 4);

            (void)type;
            return std::max<uint32_t>(variable.numeric.scalar.width / 8, 4);
        }

        /// The leaves of a block variable, named the way the GLSL names them. `base` : where this instance starts in the block.
        void FlattenBlock(const SpvReflectBlockVariable& variable, const std::string& name, uint32_t base, uint32_t arrayDim,
                          const std::set<std::string>& boolNames, std::vector<ReflectedUniform>& out)
        {
            if (arrayDim < variable.array.dims_count)
            {
                const uint32_t count = variable.array.dims[arrayDim];
                // The stride of the outer dimension : the size of everything inside one element of it
                uint32_t stride = variable.array.stride;
                for (uint32_t d = arrayDim + 1; d < variable.array.dims_count; d++)
                    stride *= std::max<uint32_t>(variable.array.dims[d], 1);

                // A runtime array (count 0) has nothing to name here
                for (uint32_t i = 0; i < count; i++)
                    FlattenBlock(variable, name + "[" + std::to_string(i) + "]", base + i * stride, arrayDim + 1, boolNames, out);
                return;
            }

            if (variable.member_count > 0)
            {
                for (uint32_t m = 0; m < variable.member_count; m++)
                {
                    const SpvReflectBlockVariable& member = variable.members[m];
                    FlattenBlock(member, name + "." + (member.name ? member.name : ""), base + member.offset, 0, boolNames, out);
                }
                return;
            }

            // The name of an array element carries its index, the bool lookup is for the declared name
            std::string declared = name;
            declared = declared.substr(0, declared.find_first_of("[."));

            ReflectedUniform uniform;
            uniform.name = name;
            uniform.type = DataTypeOf(variable.type_description, variable.numeric, boolNames.count(declared) != 0);
            uniform.offset = base;
            uniform.size = LeafSize(variable, uniform.type);
            out.push_back(std::move(uniform));
        }

        bool IsBindlessArray(const SpvReflectDescriptorBinding& binding)
        {
            return binding.name && std::string(binding.name) == kBindlessArray;
        }

        /// Reads one stage's SPIR-V, moves each of its resources to the set of its kind, and adds what it declares to the program's reflection
        bool ReflectAndRemapStage(ShaderStageKind kind, std::vector<uint32_t>& words, ShaderReflection& reflection,
                                  const std::set<std::string>& boolNames, std::string& log)
        {
            SpvReflectShaderModule module;
            if (spvReflectCreateShaderModule(words.size() * sizeof(uint32_t), words.data(), &module) != SPV_REFLECT_RESULT_SUCCESS)
            {
                log += std::string("Reflection failed on the ") + ShaderStageName(kind) + " stage\n";
                return false;
            }

            uint32_t bindingCount = 0;
            spvReflectEnumerateDescriptorBindings(&module, &bindingCount, nullptr);
            std::vector<SpvReflectDescriptorBinding*> bindings(bindingCount);
            if (bindingCount)
                spvReflectEnumerateDescriptorBindings(&module, &bindingCount, bindings.data());

            for (SpvReflectDescriptorBinding* binding : bindings)
            {
                ReflectedBinding entry;
                // A block is named by its type when it has no instance name (`buffer Lights { ... };`)
                entry.name = binding->name ? binding->name : "";
                if (entry.name.empty() && binding->type_description && binding->type_description->type_name)
                    entry.name = binding->type_description->type_name;
                entry.stages = ShaderStageBit(kind);
                entry.count = binding->count;
                for (uint32_t d = 1; d < binding->array.dims_count; d++)
                    entry.count *= binding->array.dims[d];

                uint32_t newSet = binding->set;
                uint32_t newBinding = binding->binding;
                const bool bindless = IsBindlessArray(*binding);
                bool isGlobalBlock = false;

                switch (binding->descriptor_type)
                {
                    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                        entry.kind = BindingKind::UniformBuffer;
                        entry.blockSize = binding->block.size;
                        newSet = BindingSets::Uniforms;
                        // The uniforms the shader sets by name are binding 0 of their set : the block glslang made out of the loose ones
                        // (see setGlobalUniformSet). A block the shader declares itself comes after it.
                        isGlobalBlock = binding->set == BindingSets::Uniforms && binding->binding == 0;
                        if (!isGlobalBlock)
                            newBinding = binding->binding + 1;
                        break;

                    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                        entry.kind = BindingKind::StorageBuffer;
                        entry.blockSize = binding->block.size;
                        entry.readOnly = (binding->decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE) != 0;
                        entry.writeOnly = (binding->decoration_flags & SPV_REFLECT_DECORATION_NON_READABLE) != 0;
                        newSet = BindingSets::StorageBuffers;
                        break;

                    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                        entry.kind = BindingKind::StorageImage;
                        entry.samplerType = SamplerTypeOf(binding->image);
                        entry.readOnly = (binding->decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE) != 0;
                        entry.writeOnly = (binding->decoration_flags & SPV_REFLECT_DECORATION_NON_READABLE) != 0;
                        newSet = BindingSets::StorageImages;
                        break;

                    default:
                        if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER || binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
                        {
                            // A texture and its sampler apart (Slang's Texture2D + SamplerState, GLSL's texture2D + sampler) : the engine binds them as one descriptor
                            log += "The resource '" + entry.name + "' is a " + (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER ? "sampler" : "texture") +
                                   " on its own : a texture and its sampler are one descriptor here (Sampler2D in Slang, sampler2D in GLSL)\n";
                            spvReflectDestroyShaderModule(&module);
                            return false;
                        }

                        if (bindless)
                        {
                            entry.kind = BindingKind::BindlessTextures;
                            entry.count = 0;
                            newSet = BindingSets::Bindless;
                            newBinding = 0;
                        }
                        else
                        {
                            entry.kind = BindingKind::SampledImage;
                            newSet = BindingSets::Textures;
                        }
                        entry.samplerType = SamplerTypeOf(binding->image);
                        break;
                }

                // The uniform block's members are what Shader::SetFloat & co address
                if (isGlobalBlock)
                {
                    std::vector<ReflectedUniform> declared;
                    for (uint32_t m = 0; m < binding->block.member_count; m++)
                    {
                        const SpvReflectBlockVariable& member = binding->block.members[m];
                        FlattenBlock(member, member.name ? member.name : "", member.offset, 0, boolNames, declared);
                    }

                    // The stages share one buffer : what two of them declare has to be at the same place in both
                    for (const ReflectedUniform& uniform : declared)
                    {
                        const ReflectedUniform* known = reflection.FindUniform(uniform.name);
                        if (!known)
                            reflection.uniforms.push_back(uniform);
                        else if (known->offset != uniform.offset)
                        {
                            log += "The uniform '" + uniform.name + "' is not at the same offset in the stages of the program\n";
                            spvReflectDestroyShaderModule(&module);
                            return false;
                        }
                    }
                    reflection.uniformBlockSize = std::max(reflection.uniformBlockSize, binding->block.padded_size);
                }

                if (newSet != binding->set || newBinding != binding->binding)
                {
                    if (spvReflectChangeDescriptorBindingNumbers(&module, binding, newBinding, newSet) != SPV_REFLECT_RESULT_SUCCESS)
                    {
                        log += "Could not move the resource '" + entry.name + "' to its set\n";
                        spvReflectDestroyShaderModule(&module);
                        return false;
                    }
                }

                entry.set = newSet;
                entry.binding = newBinding;

                bool merged = false;
                for (ReflectedBinding& known : reflection.bindings)
                {
                    if (known.set == entry.set && known.binding == entry.binding)
                    {
                        if (known.name != entry.name && known.kind != BindingKind::UniformBuffer)
                            log += "Warning : '" + known.name + "' and '" + entry.name + "' are both at set " + std::to_string(entry.set) +
                                   " binding " + std::to_string(entry.binding) + "\n";
                        known.stages |= entry.stages;
                        merged = true;
                        break;
                    }
                }
                if (!merged)
                    reflection.bindings.push_back(std::move(entry));
            }

            if (kind == ShaderStageKind::Vertex)
            {
                uint32_t inputCount = 0;
                spvReflectEnumerateInputVariables(&module, &inputCount, nullptr);
                std::vector<SpvReflectInterfaceVariable*> inputs(inputCount);
                if (inputCount)
                    spvReflectEnumerateInputVariables(&module, &inputCount, inputs.data());

                for (const SpvReflectInterfaceVariable* input : inputs)
                {
                    if (input->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN)
                        continue;

                    ReflectedVertexInput vertexInput;
                    vertexInput.name = input->name ? input->name : "";
                    vertexInput.location = input->location;
                    vertexInput.type = DataTypeOf(input->type_description, input->numeric, false);
                    reflection.vertexInputs.push_back(std::move(vertexInput));
                }
            }

            if (kind == ShaderStageKind::Compute && module.entry_point_count > 0)
            {
                const SpvReflectEntryPoint& entryPoint = module.entry_points[0];
                reflection.localSize[0] = entryPoint.local_size.x;
                reflection.localSize[1] = entryPoint.local_size.y;
                reflection.localSize[2] = entryPoint.local_size.z;
            }

            // The module holds the words that were edited
            const uint32_t* code = spvReflectGetCode(&module);
            const size_t size = spvReflectGetCodeSize(&module) / sizeof(uint32_t);
            words.assign(code, code + size);

            spvReflectDestroyShaderModule(&module);
            return true;
        }

        uint64_t Fnv(uint64_t h, const void* data, size_t size)
        {
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; i++)
            {
                h ^= bytes[i];
                h *= 1099511628211ULL;
            }
            return h;
        }
    }

    SourceLanguage LanguageOfFile(const std::filesystem::path& file)
    {
        const std::string extension = file.extension().string();
        if (extension == ".slang")
            return SourceLanguage::Slang;
        return SourceLanguage::GLSL;
    }

    const char* DefaultEntryPoint(SourceLanguage language, ShaderStageKind kind)
    {
        if (language != SourceLanguage::Slang)
            return "main";

        switch (kind)
        {
            case ShaderStageKind::Vertex:   return "vertexMain";
            case ShaderStageKind::Fragment: return "fragmentMain";
            case ShaderStageKind::Geometry: return "geometryMain";
            default:                        return "computeMain";
        }
    }

    std::filesystem::path FindSlangCompiler()
    {
#ifdef _WIN32
        constexpr const char* kName = "slangc.exe";
#else
        constexpr const char* kName = "slangc";
#endif
        std::error_code ec;

        if (const char* named = std::getenv("SHARD_SLANGC"); named && *named && std::filesystem::exists(named, ec))
            return named;

        if (const char* sdk = std::getenv("VULKAN_SDK"); sdk && *sdk)
        {
            const std::filesystem::path candidate = std::filesystem::path(sdk) / "Bin" / kName;
            if (std::filesystem::exists(candidate, ec))
                return candidate;

            const std::filesystem::path lowercaseBin = std::filesystem::path(sdk) / "bin" / kName;
            if (std::filesystem::exists(lowercaseBin, ec))
                return lowercaseBin;
        }

        if (const char* path = std::getenv("PATH"))
        {
#ifdef _WIN32
            constexpr char kSeparator = ';';
#else
            constexpr char kSeparator = ':';
#endif
            std::stringstream directories(path);
            std::string directory;
            while (std::getline(directories, directory, kSeparator))
            {
                if (directory.empty())
                    continue;
                const std::filesystem::path candidate = std::filesystem::path(directory) / kName;
                if (std::filesystem::exists(candidate, ec))
                    return candidate;
            }
        }

        return {};
    }

    const char* ToolchainVersion()
    {
        // Bump the last number when the way a source is turned into SPIR-V changes without the compiler changing
        return "glslang-vulkan-sdk-1.4.341.0/shard-1";
    }

    uint64_t HashSources(const std::vector<StageSource>& stages, const Options& options)
    {
        uint64_t h = 1469598103934665603ULL;
        h = Fnv(h, ToolchainVersion(), std::char_traits<char>::length(ToolchainVersion()));

        for (const StageSource& stage : stages)
        {
            const uint8_t kind = static_cast<uint8_t>(stage.kind);
            h = Fnv(h, &kind, 1);
            h = Fnv(h, stage.source.data(), stage.source.size());

            // A GLSL stage hashes as it always did (its binaries stay valid)
            if (stage.language != SourceLanguage::GLSL)
            {
                const uint8_t language = static_cast<uint8_t>(stage.language);
                h = Fnv(h, &language, 1);
            }
        }

        for (const auto& [name, value] : options.defines)
        {
            h = Fnv(h, name.data(), name.size());
            h = Fnv(h, "=", 1);
            h = Fnv(h, value.data(), value.size());
            h = Fnv(h, ";", 1);
        }

        return h;
    }

    Result CompileProgram(const std::vector<StageSource>& stages, const Options& options)
    {
        EnsureGlslangInitialised();

        Result result;
        result.binary.sourceHash = HashSources(stages, options);

        if (stages.empty())
        {
            result.log = "No stage to compile\n";
            return result;
        }

        const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);

        struct Prepared
        {
            ShaderStageKind kind;
            std::string text;
            std::string preamble;
            std::string label;
        };

        std::vector<Prepared> prepared;
        std::vector<const StageSource*> slangStages;
        std::set<std::string> boolNames;

        for (const StageSource& stage : stages)
        {
            for (const std::string& name : FindBoolUniforms(stage.source))
                boolNames.insert(name);

            if (stage.language == SourceLanguage::Slang)
            {
                slangStages.push_back(&stage);
                continue;
            }

            Prepared p;
            p.kind = stage.kind;
            p.label = stage.label;
            p.text = PrepareStageSource(stage.source);
            p.preamble = BuildPreamble(options, UsesBindlessConstructor(stage.source));

            prepared.push_back(std::move(p));
        }

        std::vector<std::unique_ptr<glslang::TShader>> shaders;
        glslang::TProgram program;

        for (const Prepared& p : prepared)
        {
            const EShLanguage language = ToGlslang(p.kind);
            auto shader = std::make_unique<glslang::TShader>(language);

            const char* text = p.text.c_str();
            const int length = static_cast<int>(p.text.size());
            const char* label = p.label.c_str();
            shader->setStringsWithLengthsAndNames(&text, &length, &label, 1);
            shader->setPreamble(p.preamble.c_str());

            shader->setEntryPoint("main");
            shader->setSourceEntryPoint("main");

            shader->setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 100);
            shader->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
            shader->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
            shader->setEnvInputVulkanRulesRelaxed();

            // What a GL driver does when it links : numbers the samplers that have none, matches the locations of one stage's
            // outputs with the next stage's inputs, and gathers the loose uniforms into one block
            shader->setAutoMapBindings(true);
            shader->setAutoMapLocations(true);
            shader->setBindingsPerResourceType();
            shader->setGlobalUniformBlockName(kUniformBlockName);
            shader->setGlobalUniformSet(BindingSets::Uniforms);
            shader->setGlobalUniformBinding(0);

            if (!shader->parse(GetDefaultResources(), 460, ECoreProfile, false, false, messages))
            {
                result.log += std::string(shader->getInfoLog()) + shader->getInfoDebugLog();
                result.log += "Failed to compile the " + std::string(ShaderStageName(p.kind)) + " stage of " + p.label + "\n";
                return result;
            }

            if (const char* warnings = shader->getInfoLog(); warnings && *warnings)
                result.log += warnings;

            program.addShader(shader.get());
            shaders.push_back(std::move(shader));
        }

        if (!prepared.empty() && !program.link(messages))
        {
            result.log += std::string(program.getInfoLog()) + program.getInfoDebugLog();
            result.log += "Failed to link the program\n";
            return result;
        }

        if (!prepared.empty() && !program.mapIO())
        {
            result.log += std::string(program.getInfoLog()) + program.getInfoDebugLog();
            result.log += "Failed to map the bindings and locations of the program\n";
            return result;
        }

        // What slangc compiles comes out as SPIR-V : it joins the stages of glslang at the reflection
        for (const StageSource* stage : slangStages)
        {
            if (!CompileSlangStage(*stage, options, result.binary.spirv[static_cast<size_t>(stage->kind)], result.log))
                return result;
        }

        for (const Prepared& p : prepared)
        {
            const glslang::TIntermediate* intermediate = program.getIntermediate(ToGlslang(p.kind));
            if (!intermediate)
            {
                result.log += "No code for the " + std::string(ShaderStageName(p.kind)) + " stage\n";
                return result;
            }

            glslang::SpvOptions spvOptions;
            spvOptions.generateDebugInfo = false;
            spvOptions.stripDebugInfo = !options.debugNames;
            spvOptions.disableOptimizer = true;
            spvOptions.optimizeSize = false;
            spvOptions.validate = false;

            spv::SpvBuildLogger logger;
            std::vector<uint32_t>& words = result.binary.spirv[static_cast<size_t>(p.kind)];
            glslang::GlslangToSpv(*intermediate, words, &logger, &spvOptions);

            result.log += logger.getAllMessages();

            if (words.empty())
            {
                result.log += "The " + std::string(ShaderStageName(p.kind)) + " stage produced no code\n";
                return result;
            }
        }

        // Each stage's resources are moved to the set of their kind : the same decisions in every stage, so that they agree
        for (size_t s = 0; s < result.binary.spirv.size(); s++)
        {
            std::vector<uint32_t>& words = result.binary.spirv[s];
            if (words.empty())
                continue;

            if (!ReflectAndRemapStage(static_cast<ShaderStageKind>(s), words, result.binary.reflection, boolNames, result.log))
                return result;
        }

        // The reflection of a block member in a stage that does not use it is as good as the one of a stage that does : the first
        // stage that has the block wins, the others only add their stage bit to the binding.
        std::sort(result.binary.reflection.bindings.begin(), result.binary.reflection.bindings.end(),
                  [](const ReflectedBinding& a, const ReflectedBinding& b) { return a.set != b.set ? a.set < b.set : a.binding < b.binding; });

        result.ok = true;
        return result;
    }

    std::string ResolveIncludes(const std::filesystem::path& file, std::string* error)
    {
        struct Context
        {
            std::unordered_set<std::string> included;
            std::vector<std::string> stack;
            std::string error;
        } context;

        std::function<std::string(const std::filesystem::path&)> resolve = [&](const std::filesystem::path& path) -> std::string
        {
            std::error_code ec;
            const std::string normalized = std::filesystem::weakly_canonical(path, ec).generic_string();

            for (const std::string& active : context.stack)
            {
                if (active == normalized)
                {
                    context.error += "Include cycle at " + path.generic_string() + "\n";
                    return "";
                }
            }

            // Each file is spliced in once, wherever it is first included from : the shared headers have no guard of their own
            if (!context.included.insert(normalized).second)
                return "";

            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                context.error += "Cannot read " + path.generic_string() + "\n";
                return "";
            }

            context.stack.push_back(normalized);

            std::ostringstream out;
            std::string line;
            int lineNumber = 0;
            const std::filesystem::path directory = path.parent_path();

            while (std::getline(stream, line))
            {
                lineNumber++;
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                size_t i = line.find_first_not_of(" \t");
                if (i != std::string::npos && line.compare(i, 8, "#include") == 0)
                {
                    const size_t open = line.find('"', i + 8);
                    const size_t close = open == std::string::npos ? std::string::npos : line.find('"', open + 1);
                    if (close != std::string::npos)
                    {
                        out << resolve(directory / line.substr(open + 1, close - open - 1));
                        out << "#line " << (lineNumber + 1) << "\n";
                        continue;
                    }
                }

                out << line << "\n";
            }

            context.stack.pop_back();
            return out.str();
        };

        std::string text = resolve(file);
        if (error)
            *error = context.error;
        return text;
    }

    std::vector<ProgramFiles> FindPrograms(const std::filesystem::path& root)
    {
        struct Extension { const char* extension; ShaderStageKind kind; };
        static const Extension kExtensions[] = {
            {".vert", ShaderStageKind::Vertex}, {".frag", ShaderStageKind::Fragment},
            {".geom", ShaderStageKind::Geometry}, {".comp", ShaderStageKind::Compute}
        };

        std::map<std::string, ProgramFiles> programs;

        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(root, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec || !it->is_regular_file())
                continue;

            const std::string extension = it->path().extension().string();

            if (LanguageOfFile(it->path()) == SourceLanguage::Slang)
            {
                // One file, every stage : which ones it has is told by the entry points it declares
                std::ifstream stream(it->path(), std::ios::binary);
                std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

                // What the comments say about the entry points is not a declaration of them
                static const std::regex comments(R"(//[^\n]*|/\*[\s\S]*?\*/)");
                text = std::regex_replace(text, comments, " ");

                std::filesystem::path relative = std::filesystem::relative(it->path(), root, ec);
                relative.replace_extension("");
                const std::string name = relative.generic_string();

                for (size_t s = 0; s < static_cast<size_t>(ShaderStageKind::Count); s++)
                {
                    const std::string entry = DefaultEntryPoint(SourceLanguage::Slang, static_cast<ShaderStageKind>(s));
                    if (std::regex_search(text, std::regex("\\b" + entry + "\\s*\\(")))
                    {
                        ProgramFiles& program = programs[name];
                        program.name = name;
                        program.stages[s] = it->path();
                    }
                }
                continue;
            }

            for (const Extension& known : kExtensions)
            {
                if (extension != known.extension)
                    continue;

                std::filesystem::path relative = std::filesystem::relative(it->path(), root, ec);
                relative.replace_extension("");
                const std::string name = relative.generic_string();

                ProgramFiles& program = programs[name];
                program.name = name;
                program.stages[static_cast<size_t>(known.kind)] = it->path();
            }
        }

        std::vector<ProgramFiles> out;
        for (auto& [name, program] : programs)
        {
            // A graphics program has to have both a vertex and a fragment stage (a depth pass that has no fragment stage would be a
            // program with a vertex stage alone, which the engine does not write)
            const bool compute = program.Has(ShaderStageKind::Compute);
            const bool graphics = program.Has(ShaderStageKind::Vertex) && program.Has(ShaderStageKind::Fragment);
            if (compute || graphics)
                out.push_back(std::move(program));
        }
        return out;
    }

    Result CompileProgramFiles(const ProgramFiles& files, const Options& options)
    {
        std::vector<StageSource> stages;
        std::string errors;

        for (size_t s = 0; s < static_cast<size_t>(ShaderStageKind::Count); s++)
        {
            if (files.stages[s].empty())
                continue;

            StageSource stage;
            stage.kind = static_cast<ShaderStageKind>(s);
            stage.label = files.stages[s].generic_string();
            stage.language = LanguageOfFile(files.stages[s]);
            stage.file = files.stages[s];

            std::string error;
            stage.source = ResolveIncludes(files.stages[s], &error);
            errors += error;
            stages.push_back(std::move(stage));
        }

        if (!errors.empty())
        {
            Result failed;
            failed.log = errors;
            return failed;
        }

        Result result = CompileProgram(stages, options);
        result.log = errors + result.log;
        return result;
    }

}
