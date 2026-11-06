#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "engine/renderer/rhi/shader/shader_binary.hpp"

namespace Shard::Engine::Rendering::ShaderCompiler {

    // GLSL is the engine's language (OpenGL reads it too). Slang is Vulkan only, compiled by slangc, entry points vertexMain / fragmentMain / geometryMain / computeMain.
    // Slang uniforms go in the constant buffer at [[vk::binding(0, 3)]], textures are `Sampler2D`. Without slangc the shaderc-built binary runs.
    enum class SourceLanguage : uint8_t { GLSL = 0, Slang };

    SourceLanguage LanguageOfFile(const std::filesystem::path& file);

    const char* DefaultEntryPoint(SourceLanguage language, ShaderStageKind kind);

    struct StageSource
    {
        ShaderStageKind kind = ShaderStageKind::Vertex;
        std::string source;
        std::string label;
        SourceLanguage language = SourceLanguage::GLSL;
        std::filesystem::path file;
    };

    struct Options
    {
        std::vector<std::pair<std::string, std::string>> defines;

        bool debugNames = true;
    };

    struct Result
    {
        bool ok = false;
        std::string log;
        ShaderBinary binary;
    };

    // Compiles GLSL 4.3 for Vulkan like a GL driver links: loose uniforms into set 3 binding 0, auto-bound samplers, matched in/out locations.
    // sampler2D(handle) reads the global bindless array.
    Result CompileProgram(const std::vector<StageSource>& stages, const Options& options = {});

    std::filesystem::path FindSlangCompiler();

    std::string ResolveIncludes(const std::filesystem::path& file, std::string* error = nullptr);

    struct ProgramFiles
    {
        std::string name;
        std::filesystem::path stages[static_cast<size_t>(ShaderStageKind::Count)];
        bool Has(ShaderStageKind kind) const { return !stages[static_cast<size_t>(kind)].empty(); }
    };

    std::vector<ProgramFiles> FindPrograms(const std::filesystem::path& root);

    Result CompileProgramFiles(const ProgramFiles& files, const Options& options = {});

    const char* ToolchainVersion();

    uint64_t HashSources(const std::vector<StageSource>& stages, const Options& options);

}
