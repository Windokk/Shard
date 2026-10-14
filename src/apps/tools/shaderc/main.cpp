// shaderc : compiles the shaders of a directory to SPIR-V, with their reflection.
//
//   shaderc <shader root> <output directory> [options]
//
// Every program found under the root (name.vert + name.frag [+ name.geom], or name.comp, or one name.slang that has the entry points of its
// stages : slangc compiles those, the Vulkan SDK has it, without it they are skipped) becomes <output>/<name>.spvpack. A program whose
// sources, defines and compiler have not changed since the file was written is left alone (that is the cache : the output directory).
//
//   --define NAME[=VALUE]   a permutation : `#define NAME VALUE` is put before the first line of every stage. Several allowed.
//   --only <name>           only the program with this name (relative to the root, no extension)
//   --reflect               prints the reflection of what is compiled (JSON) instead of writing files
//   --force                 compiles everything, whatever the cache says
//   --quiet                 only errors
//   -j <n>                  threads (default : all of them)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "engine/renderer/rhi/shader_compiler/shader_compiler.hpp"

namespace fs = std::filesystem;
using namespace Shard::Engine::Rendering;

namespace {

    struct Arguments
    {
        fs::path root;
        fs::path output;
        ShaderCompiler::Options options;
        std::string only;
        bool reflect = false;
        bool force = false;
        bool quiet = false;
        unsigned threads = 0;
    };

    bool ParseArguments(int argc, char** argv, Arguments& args)
    {
        std::vector<std::string> positional;

        for (int i = 1; i < argc; i++)
        {
            const std::string arg = argv[i];
            auto next = [&](std::string& out) {
                if (i + 1 >= argc)
                    return false;
                out = argv[++i];
                return true;
            };

            std::string value;
            if (arg == "--define" && next(value))
            {
                const size_t equals = value.find('=');
                args.options.defines.emplace_back(value.substr(0, equals), equals == std::string::npos ? "1" : value.substr(equals + 1));
            }
            else if (arg == "--only" && next(value))
                args.only = value;
            else if (arg == "-j" && next(value))
                args.threads = static_cast<unsigned>(std::max(1, std::atoi(value.c_str())));
            else if (arg == "--reflect")
                args.reflect = true;
            else if (arg == "--force")
                args.force = true;
            else if (arg == "--quiet")
                args.quiet = true;
            else if (!arg.empty() && arg[0] == '-')
            {
                std::fprintf(stderr, "Unknown option %s\n", arg.c_str());
                return false;
            }
            else
                positional.push_back(arg);
        }

        if (positional.size() != (args.reflect ? 1u : 2u) && positional.size() != 2u)
            return false;

        args.root = positional[0];
        if (positional.size() > 1)
            args.output = positional[1];
        return true;
    }

    bool ReadFile(const fs::path& path, std::vector<uint8_t>& out)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return false;
        out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return true;
    }
}

int main(int argc, char** argv)
{
    Arguments args;
    if (!ParseArguments(argc, argv, args))
    {
        std::fprintf(stderr, "usage : shaderc <shader root> <output directory> [--define NAME[=VALUE]] [--only name] [--reflect] [--force] [--quiet] [-j n]\n");
        return 2;
    }

    std::vector<ShaderCompiler::ProgramFiles> programs = ShaderCompiler::FindPrograms(args.root);
    if (!args.only.empty())
    {
        programs.erase(std::remove_if(programs.begin(), programs.end(), [&](const ShaderCompiler::ProgramFiles& p) { return p.name != args.only; }), programs.end());
    }

    if (programs.empty())
    {
        std::fprintf(stderr, "No shader program under %s\n", args.root.string().c_str());
        return 1;
    }

    std::atomic<size_t> next{0};
    std::atomic<int> compiled{0}, upToDate{0}, failed{0}, skipped{0};
    const fs::path slangc = ShaderCompiler::FindSlangCompiler();
    std::mutex printMutex;
    const auto started = std::chrono::steady_clock::now();

    auto work = [&]() {
        while (true)
        {
            const size_t index = next.fetch_add(1);
            if (index >= programs.size())
                return;

            const ShaderCompiler::ProgramFiles& program = programs[index];
            const fs::path outPath = args.output / (program.name + ".spvpack");

            // Includes are resolved first : they are part of what the cache is keyed on
            std::vector<ShaderCompiler::StageSource> stages;
            std::string includeErrors;
            for (size_t s = 0; s < static_cast<size_t>(ShaderStageKind::Count); s++)
            {
                if (program.stages[s].empty())
                    continue;
                ShaderCompiler::StageSource stage;
                stage.kind = static_cast<ShaderStageKind>(s);
                stage.label = program.stages[s].generic_string();
                stage.language = ShaderCompiler::LanguageOfFile(program.stages[s]);
                stage.file = program.stages[s];
                std::string error;
                stage.source = ShaderCompiler::ResolveIncludes(program.stages[s], &error);
                includeErrors += error;
                stages.push_back(std::move(stage));
            }

            if (!includeErrors.empty())
            {
                std::lock_guard<std::mutex> lock(printMutex);
                std::fprintf(stderr, "[FAILED] %s\n%s", program.name.c_str(), includeErrors.c_str());
                failed++;
                continue;
            }

            // A Slang program needs slangc ; a machine that has none is not told that the whole build failed for it
            if (stages.front().language == ShaderCompiler::SourceLanguage::Slang && slangc.empty())
            {
                std::lock_guard<std::mutex> lock(printMutex);
                std::fprintf(stderr, "[SKIPPED] %s : slangc was not found (the Vulkan SDK has it, SHARD_SLANGC names another)\n", program.name.c_str());
                skipped++;
                continue;
            }

            if (!args.reflect && !args.force)
            {
                std::vector<uint8_t> bytes;
                ShaderBinary existing;
                if (ReadFile(outPath, bytes) && DeserializeShaderBinary(bytes.data(), bytes.size(), existing) &&
                    existing.sourceHash == ShaderCompiler::HashSources(stages, args.options))
                {
                    upToDate++;
                    continue;
                }
            }

            ShaderCompiler::Result result = ShaderCompiler::CompileProgram(stages, args.options);

            std::lock_guard<std::mutex> lock(printMutex);

            if (!result.ok)
            {
                std::fprintf(stderr, "[FAILED] %s\n%s\n", program.name.c_str(), result.log.c_str());
                failed++;
                continue;
            }

            if (!result.log.empty() && !args.quiet)
                std::fprintf(stderr, "[%s]\n%s", program.name.c_str(), result.log.c_str());

            if (args.reflect)
            {
                std::printf("// %s\n%s\n", program.name.c_str(), ShaderReflectionToJson(result.binary.reflection).c_str());
                compiled++;
                continue;
            }

            std::error_code ec;
            fs::create_directories(outPath.parent_path(), ec);

            const std::vector<uint8_t> bytes = SerializeShaderBinary(result.binary);
            std::ofstream file(outPath, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file)
            {
                std::fprintf(stderr, "[FAILED] %s : cannot write %s\n", program.name.c_str(), outPath.string().c_str());
                failed++;
                continue;
            }

            if (!args.quiet)
                std::printf("[OK] %s\n", program.name.c_str());
            compiled++;
        }
    };

    const unsigned threadCount = std::min<size_t>(args.threads ? args.threads : std::max(1u, std::thread::hardware_concurrency()), programs.size());
    std::vector<std::thread> threads;
    for (unsigned t = 1; t < threadCount; t++)
        threads.emplace_back(work);
    work();
    for (std::thread& t : threads)
        t.join();

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (!args.quiet)
        std::printf("shaderc : %d compiled, %d up to date, %d skipped, %d failed (%.1f s, %s)\n", compiled.load(), upToDate.load(), skipped.load(),
                    failed.load(), seconds, ShaderCompiler::ToolchainVersion());

    return failed.load() == 0 ? 0 : 1;
}
