// The console commands that look at the engine. They are registered once by EngineInstance::RegisterEngineSystems.

#include "engine/world/engine.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/core/config/cvar.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/core/diagnostics/profiler.hpp"
#include "engine/core/jobs/job_system.hpp"
#include "engine/core/memory/memory_tracker.hpp"
#include "engine/renderer/components/camera_manager.hpp"
#include "engine/renderer/components/render_world_data.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/world/time_manager.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_manager.hpp"

namespace Shard::Engine::Core {

    namespace {

        std::string Fixed(double value, int decimals = 2)
        {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
            return buffer;
        }

        /// "on" / "off" / "1" / "0" / "true" / "false" ; nothing if it is none of them
        bool ParseSwitch(const std::string& text, bool& out)
        {
            std::string lower = text;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (lower == "on" || lower == "1" || lower == "true")  { out = true;  return true; }
            if (lower == "off" || lower == "0" || lower == "false") { out = false; return true; }
            return false;
        }
    }

    void EngineInstance::RegisterConsoleCommands()
    {
        CVarRegistry& registry = CVarRegistry::Global();

        registry.RegisterCommand("stat", "stat [fps|render|world|memory|jobs] : frame time, draw calls, what the world holds, memory, threads (no argument : a summary)",
            [this](const std::vector<std::string>& args) {
                const std::string what = args.empty() ? "all" : args[0];
                const bool all = what == "all";
                const Debugging::MinimalStatistics stats = m_Context.profiler ? m_Context.profiler->GetStats() : Debugging::MinimalStatistics{};

                std::string out;
                bool known = all;

                if (all || what == "fps") {
                    known = true;
                    out += "Frame   : " + Fixed(stats.fps, 1) + " fps, " + Fixed(stats.frameTimeMs) + " ms\n";
                }
                if (all || what == "render") {
                    known = true;
                    out += "Render  : " + std::to_string(stats.cmds) + " draw commands, " + std::to_string(stats.primitives) + " primitives, " +
                           std::to_string(stats.vertices) + " vertices, GPU memory " + Fixed(stats.gpuMemoryMB, 0) + " MB\n";
                }
                if (all || what == "world") {
                    known = true;
                    out += "World   : " + std::to_string(stats.actors) + " actors, " + std::to_string(stats.lights) + " lights\n";
                }
                if (all || what == "memory") {
                    known = true;
                    const MemoryTracker& tracker = MemoryTracker::Get();
                    out += std::string("Memory  : ") + (tracker.IsEnabled() ? std::to_string(tracker.LiveBytes() / 1024) + " KB tracked in " +
                           std::to_string(tracker.LiveCount()) + " allocations" : "tracking is off (memory.track)") + "\n";
                }
                if (all || what == "jobs") {
                    known = true;
                    out += std::string("Jobs    : ") + (m_Context.jobSystem ? std::to_string(m_Context.jobSystem->ThreadCount()) + " threads" : "no job system") + "\n";
                }

                if (!known)
                    return CommandResult{false, "usage: stat [fps|render|world|memory|jobs]"};
                if (!out.empty() && out.back() == '\n') out.pop_back();
                return CommandResult{true, out};
            });

        registry.RegisterCommand("gpu", "gpu : the graphics card, the driver and the size of the viewport",
            [this](const std::vector<std::string>&) {
                if (!m_Context.renderer)
                    return CommandResult{false, "no renderer"};
                std::string out = "Vendor   : " + m_Context.renderer->GetDeviceVendor() + "\n" +
                                  "Renderer : " + m_Context.renderer->GetRendererName() + "\n" +
                                  "Driver   : " + m_Context.renderer->GetDriverVersion();
                const Rendering::RenderSnapshot& snapshot = m_Context.renderer->GetSnapshot();
                out += "\nViewport : " + std::to_string(snapshot.viewportWidth) + " x " + std::to_string(snapshot.viewportHeight);
                return CommandResult{true, out};
            });

        registry.RegisterCommand("world.info", "world.info : the loaded worlds and what the renderer draws or lights them with",
            [this](const std::vector<std::string>&) {
                if (!m_Context.worldManager || m_Context.worldManager->GetLoadedWorldCount() == 0)
                    return CommandResult{true, "No world loaded"};

                std::string out;
                for (int i = 0; i < m_Context.worldManager->GetLoadedWorldCount(); ++i) {
                    Worlds::World* world = m_Context.worldManager->GetWorldAt(i);
                    if (!world) continue;
                    Rendering::RenderWorldData& data = world->Ext<Rendering::RenderWorldData>();
                    out += "[" + std::to_string(i) + "] " + world->GetName() + (world->IsDirty() ? " (unsaved changes)" : "") + "\n";
                    out += "    " + std::to_string(world->transforms.size()) + " actors, " + std::to_string(data.models.size()) + " models, " +
                           std::to_string(data.lightComps.size()) + " lights, " + std::to_string(data.cameras.size()) + " cameras, " +
                           std::to_string(data.probeVolumes.size()) + " GI probe volumes, sky " + (data.skybox ? "yes" : "none") + "\n";
                }
                if (!out.empty() && out.back() == '\n') out.pop_back();
                return CommandResult{true, out};
            });

        registry.RegisterCommand("timescale", "timescale [x] : the speed of the simulation (1 = real time, 0 = frozen)",
            [this](const std::vector<std::string>& args) {
                if (!m_Context.timeManager)
                    return CommandResult{false, "no time manager"};
                if (args.empty())
                    return CommandResult{true, "timescale = " + Fixed(m_Context.timeManager->GetTimeSpeed(), 3)};

                float speed = 1.0f;
                try { speed = std::stof(args[0]); } catch (...) { return CommandResult{false, "usage: timescale [x]"}; }
                if (speed < 0.0f || speed > 100.0f)
                    return CommandResult{false, "timescale must be between 0 and 100"};
                m_Context.timeManager->SetTimeSpeed(speed);
                return CommandResult{true, "timescale = " + Fixed(speed, 3)};
            });

        registry.RegisterCommand("render.occlusion", "render.occlusion [on|off] : GPU occlusion culling of the main camera's geometry",
            [this](const std::vector<std::string>& args) {
                if (!m_Context.renderer)
                    return CommandResult{false, "no renderer"};
                if (!args.empty()) {
                    bool on = true;
                    if (!ParseSwitch(args[0], on)) return CommandResult{false, "usage: render.occlusion [on|off]"};
                    m_Context.renderer->SetOcclusionCullingEnabled(on);
                }
                return CommandResult{true, std::string("render.occlusion = ") + (m_Context.renderer->IsOcclusionCullingEnabled() ? "on" : "off")};
            });

        registry.RegisterCommand("render.msaa", "render.msaa on|off : multisampling of the viewport",
            [this](const std::vector<std::string>& args) {
                if (!m_Context.renderer)
                    return CommandResult{false, "no renderer"};
                bool on = true;
                if (args.empty() || !ParseSwitch(args[0], on)) return CommandResult{false, "usage: render.msaa on|off"};
                m_Context.renderer->ToggleMultisampling(on);
                return CommandResult{true, std::string("render.msaa = ") + (on ? "on" : "off")};
            });

        registry.RegisterCommand("memory.report", "memory.report : what the engine's allocators still hold (needs memory.track)",
            [](const std::vector<std::string>&) {
                const MemoryTracker& tracker = MemoryTracker::Get();
                if (!tracker.IsEnabled())
                    return CommandResult{false, "Tracking is off : set memory.track to true (at startup) to record allocations"};
                const std::string report = tracker.Report();
                return CommandResult{true, report.empty() ? std::string("Nothing is allocated through the tracked allocators") : report};
            });

        registry.RegisterCommand("jobs.info", "jobs.info : the threads of the job system",
            [this](const std::vector<std::string>&) {
                if (!m_Context.jobSystem)
                    return CommandResult{false, "no job system"};
                return CommandResult{true, std::to_string(m_Context.jobSystem->ThreadCount()) + " threads can run jobs (" +
                                           std::to_string(m_Context.jobSystem->WorkerCount()) + " workers + the main thread)"};
            });

        registry.RegisterCommand("log.level", "log.level [log|info|warning|error] : the lowest level the logger records",
            [](const std::vector<std::string>& args) {
                if (args.empty())
                    return CommandResult{true, "usage: log.level log|info|warning|error"};

                std::string lower = args[0];
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                Debugging::Level level;
                if (lower == "log") level = Debugging::Level::Log;
                else if (lower == "info") level = Debugging::Level::Info;
                else if (lower == "warning" || lower == "warn") level = Debugging::Level::Warning;
                else if (lower == "error") level = Debugging::Level::Error;
                else return CommandResult{false, "usage: log.level log|info|warning|error"};

                Debugging::GetLogger().SetMinimumLevel(level);
                return CommandResult{true, "log.level = " + lower};
            });
    }
}
