#include "shader_binary.hpp"

#include <cstring>

#include <nlohmann/json.hpp>

namespace Shard::Engine::Rendering {

    namespace {

        constexpr char kMagic[4] = {'S', 'H', 'P', 'K'};
        constexpr uint32_t kVersion = 1;

        void Put32(std::vector<uint8_t>& out, uint32_t value)
        {
            for (int i = 0; i < 4; i++)
                out.push_back(static_cast<uint8_t>(value >> (8 * i)));
        }

        void Put64(std::vector<uint8_t>& out, uint64_t value)
        {
            for (int i = 0; i < 8; i++)
                out.push_back(static_cast<uint8_t>(value >> (8 * i)));
        }

        struct Reader
        {
            const uint8_t* data;
            size_t size;
            size_t position = 0;
            bool failed = false;

            uint32_t Get32()
            {
                if (position + 4 > size) { failed = true; return 0; }
                uint32_t value = 0;
                for (int i = 0; i < 4; i++)
                    value |= static_cast<uint32_t>(data[position + i]) << (8 * i);
                position += 4;
                return value;
            }

            uint64_t Get64()
            {
                const uint64_t low = Get32();
                const uint64_t high = Get32();
                return low | (high << 32);
            }
        };

        const char* const kTypeNames[] = {"none", "bool", "int", "float", "vec2", "vec3", "vec4", "mat2", "mat3", "mat4", "uint", "uvec2"};

        const char* TypeName(ShaderDataType type)
        {
            const size_t index = static_cast<size_t>(type);
            return index < sizeof(kTypeNames) / sizeof(kTypeNames[0]) ? kTypeNames[index] : "none";
        }

        ShaderDataType TypeFromName(const std::string& name)
        {
            for (size_t i = 0; i < sizeof(kTypeNames) / sizeof(kTypeNames[0]); i++)
            {
                if (name == kTypeNames[i])
                    return static_cast<ShaderDataType>(i);
            }
            return ShaderDataType::None;
        }
    }

    const char* ShaderStageName(ShaderStageKind kind)
    {
        switch (kind)
        {
            case ShaderStageKind::Vertex:   return "vertex";
            case ShaderStageKind::Fragment: return "fragment";
            case ShaderStageKind::Geometry: return "geometry";
            case ShaderStageKind::Compute:  return "compute";
            default:                        return "?";
        }
    }

    const ReflectedBinding* ShaderReflection::FindBinding(const std::string& name) const
    {
        for (const ReflectedBinding& binding : bindings)
        {
            if (binding.name == name)
                return &binding;
        }
        return nullptr;
    }

    const ReflectedUniform* ShaderReflection::FindUniform(const std::string& name) const
    {
        for (const ReflectedUniform& uniform : uniforms)
        {
            if (uniform.name == name)
                return &uniform;
        }
        return nullptr;
    }

    std::string ShaderReflectionToJson(const ShaderReflection& reflection)
    {
        nlohmann::json json;

        json["uniformBlockSize"] = reflection.uniformBlockSize;
        json["localSize"] = {reflection.localSize[0], reflection.localSize[1], reflection.localSize[2]};

        nlohmann::json bindings = nlohmann::json::array();
        for (const ReflectedBinding& binding : reflection.bindings)
        {
            nlohmann::json b;
            b["name"] = binding.name;
            b["kind"] = static_cast<int>(binding.kind);
            b["set"] = binding.set;
            b["binding"] = binding.binding;
            b["count"] = binding.count;
            b["stages"] = binding.stages;
            b["sampler"] = static_cast<int>(binding.samplerType);
            b["readOnly"] = binding.readOnly;
            b["writeOnly"] = binding.writeOnly;
            b["blockSize"] = binding.blockSize;
            bindings.push_back(std::move(b));
        }
        json["bindings"] = std::move(bindings);

        nlohmann::json uniforms = nlohmann::json::array();
        for (const ReflectedUniform& uniform : reflection.uniforms)
            uniforms.push_back({{"name", uniform.name}, {"type", TypeName(uniform.type)}, {"offset", uniform.offset}, {"size", uniform.size}});
        json["uniforms"] = std::move(uniforms);

        nlohmann::json inputs = nlohmann::json::array();
        for (const ReflectedVertexInput& input : reflection.vertexInputs)
            inputs.push_back({{"name", input.name}, {"location", input.location}, {"type", TypeName(input.type)}});
        json["vertexInputs"] = std::move(inputs);

        return json.dump(1);
    }

    std::vector<uint8_t> SerializeShaderBinary(const ShaderBinary& binary)
    {
        std::vector<uint8_t> out;
        out.insert(out.end(), kMagic, kMagic + 4);
        Put32(out, kVersion);
        Put64(out, binary.sourceHash);

        uint32_t stageCount = 0;
        for (const auto& words : binary.spirv)
            stageCount += words.empty() ? 0 : 1;
        Put32(out, stageCount);

        for (size_t i = 0; i < binary.spirv.size(); i++)
        {
            const std::vector<uint32_t>& words = binary.spirv[i];
            if (words.empty())
                continue;

            Put32(out, static_cast<uint32_t>(i));
            Put32(out, static_cast<uint32_t>(words.size()));
            for (uint32_t word : words)
                Put32(out, word);
        }

        const std::string json = ShaderReflectionToJson(binary.reflection);
        Put32(out, static_cast<uint32_t>(json.size()));
        out.insert(out.end(), json.begin(), json.end());
        return out;
    }

    bool DeserializeShaderBinary(const uint8_t* data, size_t size, ShaderBinary& out, std::string* error)
    {
        auto fail = [&](const char* message) {
            if (error)
                *error = message;
            return false;
        };

        if (size < 4 || std::memcmp(data, kMagic, 4) != 0)
            return fail("not a compiled shader (bad magic)");

        Reader reader{data, size, 4};

        if (reader.Get32() != kVersion)
            return fail("compiled shader of another version");

        out = ShaderBinary{};
        out.sourceHash = reader.Get64();

        const uint32_t stageCount = reader.Get32();
        for (uint32_t s = 0; s < stageCount && !reader.failed; s++)
        {
            const uint32_t kind = reader.Get32();
            const uint32_t wordCount = reader.Get32();
            if (reader.failed || kind >= static_cast<uint32_t>(ShaderStageKind::Count) || reader.position + size_t(wordCount) * 4 > size)
                return fail("truncated compiled shader");

            std::vector<uint32_t>& words = out.spirv[kind];
            words.resize(wordCount);
            for (uint32_t w = 0; w < wordCount; w++)
                words[w] = reader.Get32();
        }

        const uint32_t jsonSize = reader.Get32();
        if (reader.failed || reader.position + jsonSize > size)
            return fail("truncated compiled shader (reflection)");

        nlohmann::json json = nlohmann::json::parse(data + reader.position, data + reader.position + jsonSize, nullptr, false);
        if (json.is_discarded())
            return fail("compiled shader with unreadable reflection");

        ShaderReflection& reflection = out.reflection;
        reflection.uniformBlockSize = json.value("uniformBlockSize", 0u);

        if (json.contains("localSize") && json["localSize"].is_array() && json["localSize"].size() == 3)
        {
            for (int i = 0; i < 3; i++)
                reflection.localSize[i] = json["localSize"][i].get<uint32_t>();
        }

        for (const auto& b : json.value("bindings", nlohmann::json::array()))
        {
            ReflectedBinding binding;
            binding.name = b.value("name", "");
            binding.kind = static_cast<BindingKind>(b.value("kind", 0));
            binding.set = b.value("set", 0u);
            binding.binding = b.value("binding", 0u);
            binding.count = b.value("count", 1u);
            binding.stages = b.value("stages", 0u);
            binding.samplerType = static_cast<ShaderSamplerType>(b.value("sampler", 0));
            binding.readOnly = b.value("readOnly", false);
            binding.writeOnly = b.value("writeOnly", false);
            binding.blockSize = b.value("blockSize", 0u);
            reflection.bindings.push_back(std::move(binding));
        }

        for (const auto& u : json.value("uniforms", nlohmann::json::array()))
        {
            ReflectedUniform uniform;
            uniform.name = u.value("name", "");
            uniform.type = TypeFromName(u.value("type", "none"));
            uniform.offset = u.value("offset", 0u);
            uniform.size = u.value("size", 0u);
            reflection.uniforms.push_back(std::move(uniform));
        }

        for (const auto& i : json.value("vertexInputs", nlohmann::json::array()))
        {
            ReflectedVertexInput input;
            input.name = i.value("name", "");
            input.location = i.value("location", 0u);
            input.type = TypeFromName(i.value("type", "none"));
            reflection.vertexInputs.push_back(std::move(input));
        }

        return true;
    }

}
