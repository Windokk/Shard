// The console commands that look at the engine. They are registered once by EngineInstance::RegisterEngineSystems.

#include "engine/world/engine.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "engine/core/config/cvar.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/core/diagnostics/profiler.hpp"
#include "engine/core/jobs/job_system.hpp"
#include "engine/core/memory/memory_tracker.hpp"
#include "engine/renderer/components/camera_manager.hpp"
#include "engine/renderer/components/render_world_data.hpp"
#include "engine/renderer/components/probe_volume.hpp"
#include "engine/renderer/features/lighting/probe_manager.hpp"
#include "engine/renderer/features/lighting/lightmap_manager.hpp"
#include "engine/renderer/features/raytracing/raytracer.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/features/immediate/thumbnail_service.hpp"
#include "engine/renderer/rhi/gpu_memory.hpp"
#include "engine/renderer/rhi/capture/renderdoc.hpp"
#include "engine/platform/hardware/system_info.hpp"
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

        registry.RegisterCommand("render.graph", "render.graph [dot <file>] : the render graph of the last frame (the passes in order, what they read and write, the barriers, the culled passes, the transient memory) ; dot writes it as Graphviz",
            [this](const std::vector<std::string>& args) {
                if (!m_Context.renderer)
                    return CommandResult{false, "no renderer"};
                if (args.empty()) {
                    std::string out = m_Context.renderer->GetRenderGraphText();
                    if (!out.empty() && out.back() == '\n') out.pop_back();
                    return CommandResult{true, out.empty() ? "No frame rendered yet" : out};
                }
                if (args[0] != "dot" || args.size() < 2)
                    return CommandResult{false, "usage: render.graph [dot <file>]"};
                std::ofstream file(args[1], std::ios::binary);
                if (!file)
                    return CommandResult{false, "cannot write " + args[1]};
                file << m_Context.renderer->GetRenderGraphDot();
                return CommandResult{true, "wrote " + args[1] + " (dot -Tsvg " + args[1] + " -o graph.svg)"};
            });

        registry.RegisterCommand("render.pipeline", "render.pipeline [name] : the render pipeline the frames are built with, or the one picked (`render.pipeline list` shows what there is)",
            [this](const std::vector<std::string>& args) {
                if (!m_Context.renderer)
                    return CommandResult{false, "no renderer"};

                if (!args.empty() && args[0] == "list") {
                    std::string out;
                    for (const Rendering::RenderPipelineInfo& info : Rendering::RenderPipelineRegistry::Get().List())
                        out += info.name + (info.name == m_Context.renderer->GetRenderPipelineName() ? " *" : "") + " (" + info.type + ") : " + info.description + "\n";
                    if (!out.empty() && out.back() == '\n') out.pop_back();
                    return CommandResult{true, out};
                }

                if (!args.empty()) {
                    if (!Rendering::RenderPipelineRegistry::Get().Has(args[0]))
                        return CommandResult{false, "unknown render pipeline '" + args[0] + "' (render.pipeline list)"};
                    m_Context.renderer->SetRenderPipeline(args[0]);
                }

                const std::string requested = m_Context.renderer->GetRenderPipelineName();
                const std::string active = m_Context.renderer->GetActiveRenderPipelineName();
                return CommandResult{true, "render.pipeline = " + requested + (active != requested ? " (drawing with " + active + " for now)" : "")};
            });

        registry.RegisterCommand("render.dump", "render.dump <directory> : writes the render graph and the picture each pass made into a directory (a capture of the frame that needs no tool)",
            [this](const std::vector<std::string>& args) {
                if (args.empty() || !m_Context.renderer)
                    return CommandResult{false, "usage: render.dump <directory>"};
                return CommandResult{true, "wrote in " + args[0] + " : " + m_Context.renderer->DumpFrame(args[0])};
            });

        registry.RegisterCommand("render.capture", "render.capture [frames] : takes a RenderDoc capture of the next frame(s) (start with SHARD_RENDERDOC=1, or from RenderDoc)",
            [](const std::vector<std::string>& args) {
                Rendering::RenderDoc& renderDoc = Rendering::RenderDoc::Get();
                if (!renderDoc.IsAvailable())
                    return CommandResult{false, "RenderDoc is not attached : start the engine with SHARD_RENDERDOC=1 (or the path of renderdoc.dll), or from RenderDoc"};

                uint32_t frames = 1;
                if (!args.empty())
                {
                    try { frames = static_cast<uint32_t>(std::max(1, std::stoi(args[0]))); } catch (...) { return CommandResult{false, "usage: render.capture [frames]"}; }
                }

                renderDoc.TriggerCapture(frames);
                return CommandResult{true, "capturing the next " + std::to_string(frames) + " frame(s) (render.capture.open shows them in RenderDoc)"};
            });

        registry.RegisterCommand("render.capture.open", "render.capture.open : opens the last RenderDoc capture in the RenderDoc UI",
            [](const std::vector<std::string>&) {
                Rendering::RenderDoc& renderDoc = Rendering::RenderDoc::Get();
                if (!renderDoc.IsAvailable())
                    return CommandResult{false, "RenderDoc is not attached"};

                const uint32_t count = renderDoc.CaptureCount();
                if (count == 0)
                    return CommandResult{false, "no capture yet (render.capture takes one)"};

                const std::string capture = renderDoc.GetCapture(count - 1);
                renderDoc.OpenReplayUI(capture);
                return CommandResult{true, "opening " + capture};
            });

        registry.RegisterCommand("play", "play on|off : enters / leaves play mode (leaving it reloads the world)",
            [this](const std::vector<std::string>& args) {
                bool on = true;
                if (args.empty() || !ParseSwitch(args[0], on)) return CommandResult{false, "usage: play on|off"};
                SetPlayMode(on);
                return CommandResult{true, std::string("play mode ") + (on ? "on" : "off")};
            });

        registry.RegisterCommand("world.load", "world.load <path in the project> : loads that world (the current ones are unloaded)",
            [this](const std::vector<std::string>& args) {
                if (args.empty() || !m_Context.worldManager) return CommandResult{false, "usage: world.load <world>"};
                m_Context.worldManager->LoadWorldAsync(args[0]);
                return CommandResult{true, "loading " + args[0]};
            });

        registry.RegisterCommand("render.resize", "render.resize <width> <height> : resizes the viewport framebuffers as a window resize does",
            [this](const std::vector<std::string>& args) {
                if (args.size() < 2 || !m_Context.renderer) return CommandResult{false, "usage: render.resize <width> <height>"};
                int w = 0, h = 0;
                try { w = std::stoi(args[0]); h = std::stoi(args[1]); } catch (...) { return CommandResult{false, "usage: render.resize <width> <height>"}; }
                if (w < 16 || h < 16 || w > 8192 || h > 8192) return CommandResult{false, "size out of range"};
                m_Context.renderer->RescaleFramebuffers(w, h);
                return CommandResult{true, "viewport " + std::to_string(w) + "x" + std::to_string(h)};
            });

        registry.RegisterCommand("probes.bake", "probes.bake : bakes the first active GI probe volume of the loaded worlds (what the Bake button of the editor does)",
            [this](const std::vector<std::string>&) {
                if (!m_Context.worldManager)
                    return CommandResult{false, "no world manager"};
                for (int i = 0; i < m_Context.worldManager->GetLoadedWorldCount(); ++i) {
                    Worlds::World* world = m_Context.worldManager->GetWorldAt(i);
                    if (!world) continue;
                    for (auto& [id, volume] : world->Ext<Rendering::RenderWorldData>().probeVolumes)
                        if (volume && volume->Active())
                            return volume->Bake() ? CommandResult{true, "baking"} : CommandResult{false, "the bake could not start"};
                }
                return CommandResult{false, "no active probe volume"};
            });

        registry.RegisterCommand("probes.status", "probes.status : the GI probe bake, if one is running",
            [this](const std::vector<std::string>&) {
                auto probes = m_Context.renderer ? m_Context.renderer->GetProbeManager() : nullptr;
                if (!probes)
                    return CommandResult{false, "no probe manager"};
                if (!probes->IsBaking())
                    return CommandResult{true, std::string("no bake running, last bake ") + (probes->DidLastBakeSucceed() ? "succeeded" : "did not succeed")};
                return CommandResult{true, std::string("baking : ") + probes->GetBakePhase() + " " + std::to_string(int(probes->GetBakeProgress() * 100.0f)) + "%"};
            });

        registry.RegisterCommand("raytrace.render", "raytrace.render <file.hdr> [width] [height] [samples] [bounces] : path traces the world from the active camera into a linear HDR file in the project (what the editor's Offline Raytrace window does), blocking until it is done",
            [this](const std::vector<std::string>& args) {
                if (args.empty() || !m_Context.renderer || !m_Context.fileManager)
                    return CommandResult{false, "usage: raytrace.render <file.hdr> [width] [height] [samples] [bounces]"};

                Rendering::Raytracing::RaytraceSettings settings;
                try {
                    if (args.size() > 1) settings.width = uint32_t(std::stoul(args[1]));
                    if (args.size() > 2) settings.height = uint32_t(std::stoul(args[2]));
                    if (args.size() > 3) settings.samplesPerPixel = uint32_t(std::stoul(args[3]));
                    if (args.size() > 4) settings.maxBounces = uint32_t(std::stoul(args[4]));
                } catch (...) {
                    return CommandResult{false, "usage: raytrace.render <file.hdr> [width] [height] [samples] [bounces]"};
                }
                if (settings.width < 1 || settings.height < 1 || settings.samplesPerPixel < 1 || settings.maxBounces < 1)
                    return CommandResult{false, "every number has to be at least 1"};

                const Rendering::RenderSnapshot& scene = m_Context.renderer->GetSnapshot();
                if (!scene.camera.valid)
                    return CommandResult{false, "the world has no active camera"};

                Rendering::Raytracing::Raytracer raytracer;
                const Filesystem::Path output(m_Context.fileManager->GetProjectRoot().full + "/" + args[0], true);
                if (!raytracer.Start(scene, scene.camera, settings, output))
                    return CommandResult{false, raytracer.GetStatusMessage()};

                while (raytracer.IsActive())
                    raytracer.Update();

                return CommandResult{raytracer.GetStatus() == Rendering::Raytracing::RaytraceStatus::Completed, raytracer.GetStatusMessage()};
            });

        registry.RegisterCommand("lightmaps.bake", "lightmaps.bake [samples] [texels per meter] [bounces] [max page size] [max resolution] : bakes the lightmaps of the first world's static models (what the Bake button of the World Settings does)",
            [this](const std::vector<std::string>& args) {
                Worlds::World* world = m_Context.worldManager ? m_Context.worldManager->GetWorldAt(0) : nullptr;
                if (!world)
                    return CommandResult{false, "no world loaded"};

                Rendering::RenderWorldData& data = world->Ext<Rendering::RenderWorldData>();
                try {
                    if (args.size() > 0) data.lightmapSettings.samples = uint32_t(std::stoul(args[0]));
                    if (args.size() > 1) data.lightmapSettings.texelsPerMeter = std::stof(args[1]);
                    if (args.size() > 2) data.lightmapSettings.maxBounces = uint32_t(std::stoul(args[2]));
                    if (args.size() > 3) data.lightmapSettings.maxPageSize = uint32_t(std::stoul(args[3]));
                    if (args.size() > 4) data.lightmapSettings.maxResolution = uint32_t(std::stoul(args[4]));
                } catch (...) {
                    return CommandResult{false, "usage: lightmaps.bake [samples] [texels per meter] [bounces] [max page size] [max resolution]"};
                }

                return data.BakeLightmaps(*world) ? CommandResult{true, "baking"} : CommandResult{false, "the bake could not start (see the log)"};
            });

        registry.RegisterCommand("lightmaps.status", "lightmaps.status : the lightmap bake, if one is running, and the lightmaps the world uses",
            [this](const std::vector<std::string>&) {
                auto lightmaps = m_Context.renderer ? m_Context.renderer->GetLightmapManager() : nullptr;
                if (!lightmaps)
                    return CommandResult{false, "no lightmap manager"};

                std::string out;
                if (lightmaps->IsBaking())
                    out = std::string("baking : ") + lightmaps->GetBakePhase() + " " + std::to_string(int(lightmaps->GetBakeProgress() * 100.0f)) + "%";
                else
                    out = std::string("no bake running, last bake ") + (lightmaps->DidLastBakeSucceed() ? "succeeded" : "did not succeed");

                if (Worlds::World* world = m_Context.worldManager ? m_Context.worldManager->GetWorldAt(0) : nullptr) {
                    const std::string& file = world->Ext<Rendering::RenderWorldData>().lightmapData;
                    out += file.empty() ? "\nthe world has no baked lightmaps" : "\nthe world uses " + file;
                }
                return CommandResult{true, out};
            });

        registry.RegisterCommand("lightmaps.clear", "lightmaps.clear : deletes the baked lightmaps of the first world (the models go back to the probe lighting)",
            [this](const std::vector<std::string>&) {
                Worlds::World* world = m_Context.worldManager ? m_Context.worldManager->GetWorldAt(0) : nullptr;
                if (!world)
                    return CommandResult{false, "no world loaded"};
                world->Ext<Rendering::RenderWorldData>().ClearLightmaps(*world);
                return CommandResult{true, "lightmaps deleted - save the world to keep that"};
            });

        registry.RegisterCommand("world.save", "world.save : writes the first loaded world back to its file",
            [this](const std::vector<std::string>&) {
                Worlds::World* world = m_Context.worldManager ? m_Context.worldManager->GetWorldAt(0) : nullptr;
                if (!world)
                    return CommandResult{false, "no world loaded"};
                world->Serialize(world->GetPath());
                return CommandResult{true, "saved " + world->GetPath().full};
            });

        registry.RegisterCommand("render.memory", "render.memory : what the engine holds on the GPU (textures, render targets, meshes, buffers) and what the process holds in RAM and video memory",
            [](const std::vector<std::string>&) {
                const double mb = 1024.0 * 1024.0;
                return CommandResult{true, "GPU, allocated by the engine : " + Rendering::GpuMemory::Report() + "\nProcess : " +
                                           Fixed(Platform::ProcessMemoryUsage() / mb, 0) + " MB of RAM, " + Fixed(Platform::ProcessGpuMemoryUsage() / mb, 0) + " MB of video memory"};
            });

        registry.RegisterCommand("thumbnail.test", "thumbnail.test <mesh or material in the project> : renders its asset browser thumbnail now (what the browser does for a visible asset)",
            [this](const std::vector<std::string>& args) {
                if (args.empty() || !m_Context.renderer || !m_Context.fileManager)
                    return CommandResult{false, "usage: thumbnail.test <models/x.fbx | materials/x.mat>"};

                Rendering::ThumbnailRequest request;
                request.nameInProject = args[0];
                request.filePath = m_Context.fileManager->GetProjectResRoot() / args[0];
                const bool material = args[0].size() > 4 && args[0].compare(args[0].size() - 4, 4, ".mat") == 0;
                request.kind = material ? Rendering::ThumbnailKind::Material : Rendering::ThumbnailKind::Mesh;

                Rendering::ThumbnailService* thumbnails = m_Context.renderer->GetThumbnailService();
                thumbnails->BeginFrame();
                Rendering::ThumbnailRegion region;
                if (!thumbnails->Get(request, region))
                    return CommandResult{false, "no thumbnail for " + args[0]};
                return CommandResult{true, "thumbnail of " + args[0] + " in the atlas (texture " + std::to_string(thumbnails->GetAtlasTexture()) + ")"};
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
