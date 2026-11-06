#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/renderer/rhi/shader/shader.hpp"

namespace Shard::Engine::Rendering {

    enum class ShaderStageKind : uint8_t
    {
        Vertex = 0,
        Fragment,
        Geometry,
        Compute,
        Count
    };

    inline constexpr uint32_t ShaderStageBit(ShaderStageKind kind) { return 1u << static_cast<uint32_t>(kind); }
    const char* ShaderStageName(ShaderStageKind kind);

    enum class BindingKind : uint8_t
    {
        UniformBuffer = 0,
        StorageBuffer,
        SampledImage,
        StorageImage,
        BindlessTextures
    };

    // Descriptor sets by kind: 0 textures, 1 SSBOs, 2 storage images, 3 named uniforms (UBO binding 0), 4 bindless array (binding 0).
    namespace BindingSets {
        inline constexpr uint32_t Textures = 0;
        inline constexpr uint32_t StorageBuffers = 1;
        inline constexpr uint32_t StorageImages = 2;
        inline constexpr uint32_t Uniforms = 3;
        inline constexpr uint32_t Bindless = 4;
        inline constexpr uint32_t Count = 5;
    }

    struct ReflectedBinding
    {
        std::string name;
        BindingKind kind = BindingKind::UniformBuffer;
        uint32_t set = 0;
        uint32_t binding = 0;
        uint32_t count = 1;
        uint32_t stages = 0;
        ShaderSamplerType samplerType = ShaderSamplerType::None;
        bool readOnly = false;
        bool writeOnly = false;
        uint32_t blockSize = 0;
    };

    struct ReflectedUniform
    {
        std::string name;
        ShaderDataType type = ShaderDataType::None;
        uint32_t offset = 0;
        uint32_t size = 0;                  // Bytes the value takes in the block (a vec3 is 12 even if its slot is 16)
    };

    struct ReflectedVertexInput
    {
        std::string name;
        uint32_t location = 0;
        ShaderDataType type = ShaderDataType::None;
    };

    struct ShaderReflection
    {
        std::vector<ReflectedBinding> bindings;
        std::vector<ReflectedUniform> uniforms;
        uint32_t uniformBlockSize = 0;
        std::vector<ReflectedVertexInput> vertexInputs;
        uint32_t localSize[3] = {1, 1, 1};

        const ReflectedBinding* FindBinding(const std::string& name) const;
        const ReflectedUniform* FindUniform(const std::string& name) const;
    };

    struct ShaderBinary
    {
        std::array<std::vector<uint32_t>, static_cast<size_t>(ShaderStageKind::Count)> spirv;
        ShaderReflection reflection;

        uint64_t sourceHash = 0;

        bool HasStage(ShaderStageKind kind) const { return !spirv[static_cast<size_t>(kind)].empty(); }
        const std::vector<uint32_t>& Stage(ShaderStageKind kind) const { return spirv[static_cast<size_t>(kind)]; }
        bool IsCompute() const { return HasStage(ShaderStageKind::Compute); }
    };

    std::vector<uint8_t> SerializeShaderBinary(const ShaderBinary& binary);
    bool DeserializeShaderBinary(const uint8_t* data, size_t size, ShaderBinary& out, std::string* error = nullptr);

    std::string ShaderReflectionToJson(const ShaderReflection& reflection);

}
