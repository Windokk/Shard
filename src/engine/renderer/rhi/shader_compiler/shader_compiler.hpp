#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "engine/renderer/rhi/shader/shader_binary.hpp"

namespace Shard::Engine::Rendering::ShaderCompiler {

    /// What a stage is written in. The engine's own shaders are GLSL (the OpenGL backend reads them too) ; Slang (which reads HLSL
    /// syntax) is Vulkan only :
    ///   GLSL   `.vert .frag .geom .comp`   compiled in process by glslang, with the GL conventions (see CompileProgram)
    ///   Slang  `.slang`                    compiled by slangc (the Vulkan SDK has it ; SHARD_SLANGC names another), the entry points are
    ///                                      vertexMain / fragmentMain / geometryMain / computeMain, all in the one file
    /// What the Slang file writes by hand, the GLSL gets from the compiler : the uniforms the engine sets by name (Shader::SetFloat) are
    /// the members of the constant buffer at `[[vk::binding(0, 3)]]` (matrices are column major, like glm's), a texture is a `Sampler2D`
    /// (one descriptor with its sampler : a separate Texture2D and SamplerState are refused), and the other resources are put in their
    /// set by kind like the GLSL's (see BindingSets). A machine without slangc runs the shader compiled ahead of time by shaderc.
    enum class SourceLanguage : uint8_t { GLSL = 0, Slang };

    SourceLanguage LanguageOfFile(const std::filesystem::path& file);

    /// The entry point a stage of that language is looked up by
    const char* DefaultEntryPoint(SourceLanguage language, ShaderStageKind kind);

    /// @brief One source file of a program, with its includes already spliced in (see ResolveIncludes)
    struct StageSource
    {
        ShaderStageKind kind = ShaderStageKind::Vertex;
        std::string source;
        std::string label;          ///< what the error messages call it (its path)
        SourceLanguage language = SourceLanguage::GLSL;
        std::filesystem::path file; ///< Slang : the file slangc reads (it resolves its own imports)
    };

    struct Options
    {
        /// `#define name value` before the first line of every stage : a permutation of the shader (see shaderc's --define)
        std::vector<std::pair<std::string, std::string>> defines;

        /// Keep the names of the variables in the SPIR-V (the reflection reads them, so this is on unless nothing reflects the binary)
        bool debugNames = true;
    };

    struct Result
    {
        bool ok = false;
        std::string log;            ///< what the compiler says (warnings too)
        ShaderBinary binary;
    };

    /// The shader dialect the engine writes is GLSL 4.3 as the OpenGL backend reads it : the uniforms are loose, a texture unit is the
    /// `binding` of its sampler, a handle made of two uints is a bindless texture. Compiling it for Vulkan does what a GL driver does
    /// at link time (see the BindingSets for where each kind of resource ends up) :
    ///   - loose uniforms are gathered into one uniform block (set 3, binding 0) which Shader::SetFloat & co fill by name,
    ///   - the samplers that have no binding get one, the in / out variables get matching locations,
    ///   - `sampler2D(handle)` reads a texture out of the global bindless array,
    ///   - every resource is moved to the set of its kind.
    Result CompileProgram(const std::vector<StageSource>& stages, const Options& options = {});

    /// Where slangc is : $SHARD_SLANGC, else the Vulkan SDK ($VULKAN_SDK/Bin), else the PATH. Empty when there is none.
    std::filesystem::path FindSlangCompiler();

    /// The file with the `#include "relative/path"` directives replaced by the files they name (each included once)
    std::string ResolveIncludes(const std::filesystem::path& file, std::string* error = nullptr);

    /// What a program is built from, found by name : `dir/name.vert` + `name.frag` (+ `name.geom`) or `dir/name.comp`
    /// ; or one `dir/name.slang` that has the entry points of every stage (the file is then each stage's file)
    struct ProgramFiles
    {
        std::string name;                                           ///< `dir/name`, relative to the shader root, with / separators
        std::filesystem::path stages[static_cast<size_t>(ShaderStageKind::Count)];
        bool Has(ShaderStageKind kind) const { return !stages[static_cast<size_t>(kind)].empty(); }
    };

    /// Every program under `root`. Files that are only included (.glsl) are not programs.
    std::vector<ProgramFiles> FindPrograms(const std::filesystem::path& root);

    /// Resolves the includes of the files of `files`, hashes what the program is made of with `options`, then compiles it
    Result CompileProgramFiles(const ProgramFiles& files, const Options& options = {});

    /// A name that changes when the toolchain does : part of the hash of every binary, so that a new compiler rebuilds them all
    const char* ToolchainVersion();

    uint64_t HashSources(const std::vector<StageSource>& stages, const Options& options);

}
