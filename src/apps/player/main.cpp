#include "engine/core/config/cvar.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/game_module.hpp"
#include "engine/platform/windowing/sdl/sdl_platform.hpp"
#include "apps/player/player_window.hpp"

using namespace Shard;
using namespace Shard::Engine;
using namespace Shard::Engine::Core;
using namespace Shard::Engine::Rendering;
using namespace Shard::Engine::Input;
using namespace Shard::Engine::Objects::Components;
using namespace Shard::Engine::Objects;
using namespace Shard::Game;

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>
#include "engine/renderer/rhi/gpu_memory.hpp"
#include "engine/renderer/rhi/render_thread.hpp"
#include "engine/renderer/rhi/resources/texture/image_export.hpp"
#include "engine/renderer/rhi/gpu_profiler.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include <vector>
#include "engine/platform/hardware/system_info.hpp"
#include "engine/core/diagnostics/profiler.hpp"
#include <iostream>
#include <optional>

Debugging::Level minDebugLevel = Debugging::Level::Log;
std::string mainModuleLib = "";

// --view-mode : a debug view of the scene (what the editor's viewport "View Mode" shows), for tracking a rendering
// problem down in the player itself. None : the normal render.
std::optional<ViewMode> debugViewMode;

// --frames : render that many frames, then shut down. It is a non-interactive run (what the CI's smoke test does, see
// scripts/smoke_test.sh) : nothing waits for Enter, the exit code says whether it went well, and the project is not
// written to (EngineCreationSettings::readOnly). -1 : a normal run.
int maxFrames = -1;

// --bench : with --frames, prints the average / worst frame times and the memory the process holds at the end
// ("[BENCH] ..." lines), the first --bench-warmup frames (shader compiles, streaming) left out.
bool benchmark = false;
int benchWarmup = 120;

// --screenshot : writes the last rendered frame (the viewport, before it is presented) to that PNG
std::string screenshotPath;

bool ParseViewMode(const std::string& name, ViewMode& out)
{
    static const struct { const char* name; ViewMode mode; } modes[] = {
        { "lit", ViewMode::Lit },
        { "unlit", ViewMode::Unlit },
        { "wireframe", ViewMode::Wireframe },
        { "shaded-wireframe", ViewMode::ShadedWireframe },
        { "normals", ViewMode::Normals },
        { "depth", ViewMode::Depth },
        { "uvs", ViewMode::UVs },
        { "gi", ViewMode::GlobalIllumination },
        { "ssao", ViewMode::SSAO },
        { "lightmap-uvs", ViewMode::LightmapUVs },
    };

    for (const auto& m : modes)
    {
        if (name == m.name)
        {
            out = m.mode;
            return true;
        }
    }
    return false;
}

EngineCreationSettings ComputeEngineSettings(int argc, char* argv[]) {
    // "+name=value" or "+name value" : sets a console variable (see engine/core/config/cvar.hpp) before the engine starts
    {
        std::vector<std::string> cvarWarnings;
        Shard::Engine::Core::CVarRegistry::Global().ParseCommandLine(argc, argv, &cvarWarnings);
        for (const std::string& w : cvarWarnings) std::cerr << "[WARNING] " << w << std::endl;
    }
    Engine::Core::EngineCreationSettings settings;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            settings.windowWidth = std::stoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            settings.windowHeight = std::stoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--fullscreen") == 0) {
            settings.fullscreen = true;
        }
        else if (strcmp(argv[i], "--vsync") == 0) {
            settings.vsync = false;
        }
        else if (strcmp(argv[i], "--project") == 0 && i + 1 < argc) {
            settings.project = argv[++i];
        }
        else if (strcmp(argv[i], "--bench") == 0) {
            benchmark = true;
        }
        else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshotPath = argv[++i];
        }
        else if (strcmp(argv[i], "--bench-warmup") == 0 && i + 1 < argc) {
            benchWarmup = std::stoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--world") == 0 && i + 1 < argc) {
            settings.startWorld = argv[++i];
        }
        else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            settings.scriptFile = argv[++i];
        }
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            maxFrames = std::stoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--gravity") == 0 && i + 3 < argc) {
            float x = std::stof(argv[++i]);
            float y = std::stof(argv[++i]);
            float z = std::stof(argv[++i]);
            settings.gravity = glm::vec3(x, y, z);
            settings.overrideGravity = true;
        }
        else if (strcmp(argv[i], "--debug") == 0 && i + 1 < argc) {
            std::string level = argv[++i];
            if (level == "log") {
                minDebugLevel = Debugging::Level::Log;
            } else if (level == "info") {
                minDebugLevel = Debugging::Level::Info;
            } else if (level == "warning") {
                minDebugLevel = Debugging::Level::Warning;
            } else if (level == "error") {
                minDebugLevel = Debugging::Level::Error;
            } else if (level == "fatal") {
                minDebugLevel = Debugging::Level::Fatal;
            } else {
                std::cerr << "Unknown debug level: " << level << ". Using default (Log).\n";
            }
        }
        else if (strcmp(argv[i], "--view-mode") == 0 && i + 1 < argc) {
            std::string name = argv[++i];
            ViewMode mode;
            if (ParseViewMode(name, mode))
                debugViewMode = mode;
            else
                std::cerr << "Unknown view mode: " << name << ". Use lit, unlit, wireframe, shaded-wireframe, normals, depth, uvs, gi or ssao." << std::endl;
        }
        else if(strcmp(argv[i], "--game") == 0 && i + 1 < argc){
            mainModuleLib = argv[++i];
        }
        else if(strcmp(argv[i], "--api") == 0 && i + 1 < argc){
            std::string api = argv[++i];
            if(api == "opengl")
                settings.api = 0;
            if(api == "vulkan")
                settings.api = 1;
            if(api == "dx11")
                settings.api = 2;
            if(api == "dx12")
                settings.api = 3;
        }
    }

    return settings;
}

void early_crash(){
    if (maxFrames >= 0) {
        std::cout << "Shard Engine has crashed." << std::endl;
        std::exit(1);
    }

    std::cout << "Shard Engine has crashed. Press Enter to exit..." << std::endl;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();
    std::terminate();
}

int main(int argc, char* argv[]) {

    // Engine init parameters
    EngineCreationSettings engineSettings = ComputeEngineSettings(argc, argv);
    engineSettings.readOnly = maxFrames >= 0;

    if(engineSettings.project == ""){
        std::cerr<<"No project specified, aborting..."<<std::endl;
        early_crash();
    }

    
    EngineInstance* engine = &EngineInstance::GetInstance();

    Engine::Core::SetEngine(engine);
    Engine::Debugging::SetLogger(&Engine::Debugging::Logger::GetInstance());

    //Game module loading and init
    if (!LoadGameModule(mainModuleLib)) {
        early_crash();
    }

    // Platform creation
    engineSettings.platform = new Shard::Engine::Core::Platform::SDLPlatform<Shard::Game::Core::Platform::PlayerWindow>();

    // Engine startup
    Engine::Core::GetEngine().Init(engineSettings);

    // Playing needs a world : a project that loaded, but whose world did not (a bad path, a broken file), stops here
    if (Engine::Core::GetEngine().GetWorldManager()->GetLoadedWorldCount() == 0) {
        std::cerr << "No world was loaded, aborting..." << std::endl;
        early_crash();
    }

    if (debugViewMode)
    {
        DebugViewState debugView;
        debugView.mode = *debugViewMode;
        Engine::Core::GetEngine().GetRenderer()->SetDebugView(debugView);
    }

    Engine::Core::GetEngine().SetPlayMode(true);

    //Main Loop
    int frames = 0;
    std::vector<float> frameTimes;
    Engine::Debugging::FrameProfile profileSum;
    std::map<std::string, double> gpuSums;
    uint64_t gpuSeen = 0;
    uint64_t gpuFrames = 0;
    while (!Engine::Core::GetEngine().ShouldEnd()) {
        if (maxFrames >= 0 && frames >= maxFrames) break;
        const auto frameStart = std::chrono::steady_clock::now();
        if (!Engine::Core::GetEngine().Run()) break;
        if (benchmark && frames >= benchWarmup)
            frameTimes.push_back(std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - frameStart).count());
        if (benchmark && frames >= benchWarmup) {
            const auto& p = Engine::Core::GetEngine().GetProfiler()->GetLastFrameProfile();
            for (size_t c = 0; c < Engine::Debugging::kProfileCategoryCount; c++) profileSum.categoryMs[c] += p.categoryMs[c];
            for (size_t c = 0; c < Engine::Debugging::kRenderSubSampleCount; c++) profileSum.renderSubMs[c] += p.renderSubMs[c];
            if (auto* gpu = Engine::Core::GetEngine().GetRenderer()->GetRendererAPI()->GetGpuProfiler(); gpu && gpu->LatestSequence() != gpuSeen) {
                gpuSeen = gpu->LatestSequence();
                gpuFrames++;
                for (const auto& t : gpu->Latest()) gpuSums[t.name] += t.ms;
            }
        }
        frames++;
    }

    if (!screenshotPath.empty()) {
        std::vector<uint8_t> pixels;
        auto viewport = Engine::Core::GetEngine().GetRenderer()->GetViewportFramebuffer();
        Engine::Rendering::RenderThread::Get().Flush();     // the last frame is drawn before it is read back
        if (viewport && Engine::Rendering::OnGraphicsThread([&] { return viewport->ReadPixelsRGBA8(pixels); }))
        {
            // ReadPixelsRGBA8 gives the top row first, and WritePNG flips (it expects the bottom row first, like a GL texture) :
            // flip back, or the picture comes out upside down
            const size_t rowBytes = size_t(viewport->GetWidth()) * 4;
            for (size_t y = 0; y < size_t(viewport->GetHeight()) / 2; y++)
                std::swap_ranges(pixels.begin() + y * rowBytes, pixels.begin() + (y + 1) * rowBytes, pixels.begin() + (size_t(viewport->GetHeight()) - 1 - y) * rowBytes);
            Engine::Rendering::ImageExport::WritePNG(Filesystem::Path(screenshotPath), viewport->GetWidth(), viewport->GetHeight(), 4, pixels.data());
        }
        else
            std::cerr << "Screenshot failed" << std::endl;
    }

    if (benchmark && !frameTimes.empty()) {
        std::vector<float> sorted = frameTimes;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0.0;
        for (float t : sorted) sum += t;
        const double avg = sum / sorted.size();
        const float p99 = sorted[std::min(sorted.size() - 1, size_t(sorted.size() * 0.99))];
        std::cout << "[BENCH] frames " << sorted.size() << " avg " << avg << " ms (" << 1000.0 / avg << " fps), median " << sorted[sorted.size() / 2]
                  << " ms, p99 " << p99 << " ms, worst " << sorted.back() << " ms" << std::endl;
        for (size_t c = 0; c < Engine::Debugging::kProfileCategoryCount; c++)
            std::cout << "[BENCH]   " << Engine::Debugging::ProfileCategoryName((Engine::Debugging::ProfileCategory)c) << " " << profileSum.categoryMs[c] / sorted.size() << " ms" << std::endl;
        for (size_t c = 0; c < Engine::Debugging::kRenderSubSampleCount; c++)
            std::cout << "[BENCH]     render/" << Engine::Debugging::RenderSubSampleName((Engine::Debugging::RenderSubSample)c) << " " << profileSum.renderSubMs[c] / sorted.size() << " ms" << std::endl;
        for (const auto& [name, ms] : gpuSums)
            std::cout << "[BENCH]   gpu/" << name << " " << ms / gpuFrames << " ms" << std::endl;
        if (Engine::Rendering::RenderThread::Get().IsRunning()) {
            const auto t = Engine::Rendering::RenderThread::Get().GetStats();
            std::cout << "[BENCH] render thread : " << t.averageFrameMs << " ms per frame, busy " << t.busyFraction * 100.0 << "%, simulation waited "
                      << t.averageWaitMs << " ms per frame, parked in invokes " << t.averageInvokeMs << " ms" << std::endl;
        } else {
            std::cout << "[BENCH] no render thread" << std::endl;
        }
        {
            const auto stats = Engine::Core::GetEngine().GetProfiler()->GetStats();
            std::cout << "[BENCH] scene : " << stats.cmds << " draw commands, " << stats.primitives << " indices, " << stats.vertices << " vertices, " << stats.actors << " actors" << std::endl;
        }
        std::cout << "[BENCH] graphics calls from the wrong thread : " << Engine::Rendering::RenderThread::Get().WrongThreadCount() << std::endl;
        std::cout << "[BENCH] gpu memory (engine) " << Engine::Rendering::GpuMemory::Report() << std::endl;
        std::cout << "[BENCH] ram " << Engine::Core::Platform::ProcessMemoryUsage() / (1024.0 * 1024.0) << " MB, vram "
                  << Engine::Core::Platform::ProcessGpuMemoryUsage() / (1024.0 * 1024.0) << " MB" << std::endl;
    }

    //Cleaning
    Engine::Core::GetEngine().Destroy();

    UnloadGameModule();

    if (maxFrames >= 0) {
        std::cout << "Rendered " << frames << " frames, shut down cleanly." << std::endl;
        return 0;
    }

    std::cout << "Shard Engine has finished. Press Enter to exit..." << std::endl;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();

    return 0;
}