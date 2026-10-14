#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/renderer/rhi/shader/shader.hpp"

namespace Shard::Engine::Rendering {

    /// @brief The shader stages a program is made of
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

    /// @brief What a shader resource is, from the point of view of the binding model
    enum class BindingKind : uint8_t
    {
        UniformBuffer = 0,
        StorageBuffer,
        SampledImage,       ///< a texture with its sampler (a GLSL `sampler2D`)
        StorageImage,       ///< an image a shader loads from / stores to (a GLSL `image2D`)
        BindlessTextures    ///< the global array every texture of the engine is in (see Texture2D::GetBindlessHandle)
    };

    /// @brief Where the resources of each kind live, so that a binding number (which the GLSL files give per kind : a texture unit,
    /// a storage buffer binding, an image unit) never collides with another kind's :
    ///   set 0 : textures (binding = texture unit)         set 1 : storage buffers (binding = SSBO binding)
    ///   set 2 : storage images (binding = image unit)     set 3 : the uniforms the shader sets by name, one uniform buffer at binding 0
    ///   set 4 : the bindless texture array (binding 0)
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
        uint32_t count = 1;                 ///< array size, 0 : unbounded
        uint32_t stages = 0;                ///< ShaderStageBit mask of the stages that use it
        ShaderSamplerType samplerType = ShaderSamplerType::None;
        bool readOnly = false;              ///< storage resources
        bool writeOnly = false;
        uint32_t blockSize = 0;             ///< buffers : size of the block without its runtime array
    };

    /// @brief One value of the uniform block of a shader : a leaf (a scalar, a vector, a matrix) of what the GLSL declares as loose uniforms.
    /// An array element or a member of a struct is a member of its own, named the way the GLSL names it (`lights[2].color`).
    struct ReflectedUniform
    {
        std::string name;
        ShaderDataType type = ShaderDataType::None;
        uint32_t offset = 0;                ///< bytes, in the uniform block
        uint32_t size = 0;                  ///< bytes the value takes in the block (a vec3 : 12 even if its slot is 16)
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
        uint32_t uniformBlockSize = 0;      ///< 0 : the shader sets nothing by name
        std::vector<ReflectedVertexInput> vertexInputs;
        uint32_t localSize[3] = {1, 1, 1};  ///< compute

        const ReflectedBinding* FindBinding(const std::string& name) const;
        const ReflectedUniform* FindUniform(const std::string& name) const;
    };

    /// @brief A compiled program : the SPIR-V of every stage, and what the shaders declare (see ShaderReflection).
    struct ShaderBinary
    {
        std::array<std::vector<uint32_t>, static_cast<size_t>(ShaderStageKind::Count)> spirv;
        ShaderReflection reflection;

        /// What the binary was built from (sources, defines, compiler) : a binary whose hash is not the one of the sources is stale
        uint64_t sourceHash = 0;

        bool HasStage(ShaderStageKind kind) const { return !spirv[static_cast<size_t>(kind)].empty(); }
        const std::vector<uint32_t>& Stage(ShaderStageKind kind) const { return spirv[static_cast<size_t>(kind)]; }
        bool IsCompute() const { return HasStage(ShaderStageKind::Compute); }
    };

    /// The file format of a compiled program (`.spvpack`) : a small header, the SPIR-V words of every stage, then the reflection as JSON
    std::vector<uint8_t> SerializeShaderBinary(const ShaderBinary& binary);
    bool DeserializeShaderBinary(const uint8_t* data, size_t size, ShaderBinary& out, std::string* error = nullptr);

    /// The reflection alone, as the JSON that is stored in the file (also what `shaderc --reflect` prints)
    std::string ShaderReflectionToJson(const ShaderReflection& reflection);

}
