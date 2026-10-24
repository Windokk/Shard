#include "engine/c_abi_render.hpp"
#include "engine/world/engine.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/assets/asset_importer.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

#include "engine/assets/resources_manager.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/core/determinism/determinism.hpp"
#include "engine/core/ecs/registry.hpp"
#include "engine/core/ecs/scheduler.hpp"
#include "engine/core/jobs/job_system.hpp"
#include "engine/core/jobs/thread_pool.hpp"
#include "engine/core/config/cvar.hpp"
#include "engine/core/memory/frame_allocator.hpp"
#include "engine/core/memory/memory_tracker.hpp"
#include "engine/platform/thread/thread.hpp"
#include "engine/platform/time/clock.hpp"
#include "engine/platform/process/process.hpp"
#include "engine/assets/serialization/project/project_serializer.hpp"
#include "engine/assets/hot_reloader.hpp"
#include "engine/renderer/components/model_component.hpp"
#include "engine/assets/asset_database_serializer.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/world/event_system.hpp"
#include "engine/core/diagnostics/profiler.hpp"
#include "engine/platform/windowing/iplatform.hpp"
#include "engine/platform/hardware/system_info.hpp"
#include "engine/world/time_manager.hpp"
#include "engine/renderer/components/camera_manager.hpp"
#include "engine/renderer/components/render_extractor.hpp"
#include "engine/renderer/features/lighting/probe_manager.hpp"
#include "engine/renderer/features/lighting/lightmap_manager.hpp"
#include "engine/renderer/components/probe_volume.hpp"
#include "engine/renderer/components/reflection_probe.hpp"
#include "engine/world/actor.hpp"
#include "engine/renderer/rhi/resources/texture/texture_compression.hpp"
#include "engine/renderer/rhi/resources/mesh/lightmap_uv.hpp"
#include <filesystem>
#include "engine/audio/audio_manager.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/rhi/capture/renderdoc.hpp"
#include "engine/renderer/rhi/render_thread.hpp"
#include "engine/audio/audio_world_data.hpp"
#include "engine/physics/physics_world_data.hpp"
#include "engine/renderer/components/render_world_data.hpp"
#include "engine/simulation_state.hpp"

using namespace std::chrono;

namespace Shard::Engine{

    namespace Debugging {
        Logger* g_Logger = nullptr;
    }

    namespace Core{
        
        namespace {
            // Published by the engine for the console (`list jobs`) : what the job system actually got
            CVar<int> cvJobThreads("jobs.threads", 0, "Threads that run jobs, the main thread included", CVarFlags::ReadOnly);

            // Above 0 : every frame lasts that many seconds instead of what the clock says, so a run is reproducible
            // (benchmarks, image comparisons) whatever the machine does meanwhile
            CVar<float> cvFrameDt("time.frame_dt", 0.0f, "Length in seconds of every frame, 0 : real time (deterministic runs)", CVarFlags::None, 0.0f, 1.0f);

            // Draws on a thread of its own : the simulation of the next frame runs while the previous one is drawn
            CVar<bool> cvRenderThread("render.thread", true, "Draw on a dedicated render thread (applies at startup)", CVarFlags::Archive);
            // Waits for the display to draw a picture (no tearing) or presents as soon as it can. Takes effect at the next frame.
            CVar<bool> cvVsync("render.vsync", true, "Vertical sync : wait for the display before showing a frame", CVarFlags::Archive);
            // Frame rate limits, by sleeping. Useful with the vsync off, where nothing else paces the loops.
            // engine.maxFPS paces the whole frame on the main thread (simulation included, the render thread follows it) ;
            // render.maxFPS paces the drawing alone, on the render thread, the simulation keeps its own rate.
            CVar<int> cvEngineMaxFPS("engine.maxFPS", 0, "Global frame rate limit, main thread included, 0 : unlimited", CVarFlags::Archive, 0, 10000);
            CVar<int> cvMaxFPS("render.maxFPS", 0, "Frame rate limit of the render thread alone, 0 : unlimited (the vsync, when on, limits it to the display's rate anyway)", CVarFlags::Archive, 0, 10000);

            // Holds the calling thread until `fps` frames per second means this frame is over. std::this_thread::sleep_until
            // rounds up to the Windows scheduler tick (15.6 ms) : any limit under ~400 fps came out as 64 fps. PreciseSleep waits
            // on a high resolution timer and spins on the clock for the end.
            void HoldUntil(std::chrono::steady_clock::time_point frameStart, int fps)
            {
                SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Waiting);
                const auto target = frameStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / fps));
                const double remaining = std::chrono::duration<double>(target - std::chrono::steady_clock::now()).count();
                Core::Platform::PreciseSleep(remaining);
            }
            CVar<bool> cvRenderDoc("render.renderdoc", false, "Load RenderDoc before the graphics API starts, so that `render.capture` can take a capture (applies at startup, SHARD_RENDERDOC=1 does the same)");
            CVar<bool> cvVulkanValidation("render.vulkan_validation", false, "Vulkan : turn the validation layer on (needs the Vulkan SDK ; applies at startup, SHARD_VULKAN_VALIDATION=1 does the same)");

            // Watches the resources on disk : changed textures / shaders / materials are rebuilt in place, new, renamed and
            // deleted files follow in the asset database (applies at startup)
            CVar<bool> cvHotReload("assets.hot_reload", true, "Reload the assets changed on disk while the engine runs (applies at startup)", CVarFlags::Archive);

            // How many frames may be submitted and not drawn yet : the simulation waits when it is that far ahead.
            // 1 is the lowest latency, 2 smooths out frames that vary in length.
            CVar<int> cvFramesInFlight("render.frames_in_flight", 2, "Frames the simulation may run ahead of the render thread",
                                       CVarFlags::Archive, 1, 4);

            // The simulation steps at a fixed rate, the frames do not : with this on, what a physics body drives is drawn between
            // its last two steps (by how far the frame is into the next one) instead of at the last one, which is what keeps the
            // motion even when the frame rate is not a multiple of the physics rate
            CVar<float> cvRebaseDistance("render.rebase_distance", 4096.0f, "Large worlds : meters the camera may get from the render origin before the origin moves to it (0 : always at the camera)", CVarFlags::Archive, 0.0f, 1.0e9f);
            CVar<bool> cvPhysicsInterpolate("physics.interpolate", true, "Draw physics bodies between their last two steps (smooth motion at any frame rate)", CVarFlags::Archive);

            // Per-frame scratch memory budget, read when the engine starts (a change applies at the next start)
            CVar<int> cvFrameKB("memory.frame_kb", 4096, "Per-frame scratch memory, in KB, per buffered frame (applies at startup)",
                                CVarFlags::Archive, 64, 1048576);

            // The extension points of the plugin manager the engine reads itself : the asset types and importers plugins bring
            uint32_t gAssetTypeListener = 0;
            uint32_t gAssetImporterListener = 0;

            std::string ConfigPath(const Filesystem::Path& directory, const char* fileName)
            {
                return (directory / std::string(fileName)).GetNativePath();
            }

            void LoadConfigLayer(ConfigLayer layer, const std::string& path)
            {
                if(!Filesystem::Path(path).Exists())
                    return;
                std::vector<std::string> warnings;
                auto result = CVarRegistry::Global().LoadFile(layer, path, &warnings);
                if(!result)
                    DEBUG_WARNING("Config : " + result.GetError().message);
                for(const std::string& w : warnings)
                    DEBUG_WARNING("Config " + path + " : " + w);
            }
        }

        using namespace Worlds;
        using namespace Engine::Rendering;
        using namespace Physics;
        using namespace Filesystem;
        using namespace Audio;
        using namespace Input;
        using namespace Events;

        void EngineInstance::Init(EngineCreationSettings settings){

            // First of all : whatever draws a random number (a GUID, a system's stream) derives it from the seed
            {
                const uint64_t seed = settings.seed ? *settings.seed : Determinism::Seed();
                Determinism::Configure(seed, settings.deterministic);
                DEBUG_INFO(std::string(settings.deterministic ? "Deterministic run" : "Run") + ", seed " + std::to_string(seed) +
                           (settings.seed ? "" : " (drawn : --seed " + std::to_string(seed) + " repeats it)"));
            }

            InitSystems();

            // The plugins (loaded before the engine started) get the scheduler and the event dispatcher : the systems and the
            // subscriptions they declared in OnLoad are added now
            Plugins::PluginManager::Get().OnEngineSystemsReady(this, m_Context.scheduler, m_Context.eventDispatcher);

            // "asset.type" : a kind of asset with its loader and its file extensions ; "asset.importer" : a converter from a source format
            {
                Plugins::ExtensionRegistry& extensions = Plugins::PluginManager::Get().Extensions();
                gAssetTypeListener = extensions.Listen("asset.type", [this](const Plugins::ExtensionRegistry::Item& item, bool added) {
                    if(item.typeName != typeid(Core::Resources::AssetTypeExtension).name())
                        return;
                    auto type = std::static_pointer_cast<Core::Resources::AssetTypeExtension>(item.object);
                    if(added){
                        type->kind = m_Context.resourcesManager->AllocateUserKind();
                        m_Context.resourcesManager->RegisterKind(type->kind, type->info);
                        for(const std::string& extension : type->extensions){
                            if(!Filesystem::RegisterCustomExtension(extension))
                                DEBUG_WARNING("Asset type '", type->name, "' : the extension ", extension, " is one the engine reads itself, not registered");
                        }
                        DEBUG_INFO("Asset type : ", type->name);
                    }
                    else{
                        m_Context.resourcesManager->UnregisterKind(type->kind);
                        for(const std::string& extension : type->extensions)
                            Filesystem::UnregisterCustomExtension(extension);
                    }
                });
                gAssetImporterListener = extensions.Listen("asset.importer", [](const Plugins::ExtensionRegistry::Item& item, bool added) {
                    if(item.typeName != typeid(Filesystem::AssetImporterExtension).name())
                        return;
                    auto importer = std::static_pointer_cast<Filesystem::AssetImporterExtension>(item.object);
                    if(added)
                        Filesystem::AssetImporters::Get().Register(importer);
                    else
                        Filesystem::AssetImporters::Get().Unregister(importer);
                });
            }

            if(settings.platform == nullptr)
                DEBUG_FATAL("Platform settings are nullptr !");

            m_EngineSettings = settings;
            m_Context.platform = settings.platform;
            std::shared_ptr<Shard::Engine::Projects::Project> project = Serialization::DeserializeProject(Filesystem::Path(settings.project, true));
            if(!project){
                DEBUG_FATAL("Project is nullptr, aborting...");
            }
            else{
                m_Context.currentProject = project;
                // Before any world (or plugin content) is read : the names in the files are resolved against the tree
                Projects::ApplyTagSettings(*project->GetTagSettings());
                LoadConfigLayer(ConfigLayer::Project, ConfigPath(project->GetProjectRoot(), "project.cfg"));
            }

            m_Context.fileManager->Init(m_Context.currentProject->GetProjectResourcesPath(), m_Context.fileManager->GetCurrentExecutablePath() / "engine_resources", m_Context.currentProject->GetProjectRoot());
            
            // Textures are block compressed once and kept next to the thumbnails (derived data, safe to delete)
            Rendering::TextureCache::SetDirectory((std::filesystem::path(m_Context.currentProject->GetProjectRoot().full) / ".cache" / "textures").generic_string());
            // The lightmap layout of a mesh is worked out once and kept there too, instead of at every load
            Rendering::SetLightmapUnwrapCacheDirectory((std::filesystem::path(m_Context.currentProject->GetProjectRoot().full) / ".cache" / "lightmap_uv").generic_string());

            m_Context.resourcesManager->ConstructGlobalFileIndex(m_Context.currentProject->GetProjectResourcesPath(), m_Context.currentProject->GetAssetDatabasePath());

            // RenderDoc has to be in the process before the graphics API is started : if the engine was started from it, it is there already
            {
                const char* renderDocEnvironment = std::getenv("SHARD_RENDERDOC");
                const bool renderDocWanted = renderDocEnvironment != nullptr || cvRenderDoc.Get();
                if(Rendering::RenderDoc::Get().Attach(renderDocWanted, (renderDocEnvironment && std::string(renderDocEnvironment) != "1") ? renderDocEnvironment : ""))
                    Rendering::RenderDoc::Get().SetCapturePrefix((std::filesystem::path(m_Context.currentProject->GetProjectRoot().full) / ".cache" / "captures" / "shard").generic_string());
            }

            // The command line (--vsync) and the cvar both can turn it off ; the cvar says what is in force afterwards
            settings.vsync = settings.vsync && cvVsync.Get();
            cvVsync.ForceSet(settings.vsync);

            m_Context.platform->CreateWindow("Shard", settings.windowWidth, settings.windowHeight, settings.fullscreen, settings.vsync, settings.api);
            {
                Platform::IWindow* window = m_Context.platform->GetWindow();
                if(window)
                    cvVsync.AddOnChange([window](const CVarBase&) { window->SetVSync(cvVsync.Get()); });
            }

            Projects::PhysicsSettings* physicsSettings = m_Context.currentProject->GetPhysicsSettings();

            if(settings.overrideGravity)
                physicsSettings->gravity = settings.gravity;

            m_Context.physicsManager->Init(physicsSettings->gravity, settings.deterministic);
            m_Context.audioManager->Init(100.0f, settings.headless);
            m_RendererSettings = std::make_shared<RendererSettings>();
            m_RendererSettings->viewportWidth = m_Context.platform->GetWindow()->GetFramebufferWidth();
            m_RendererSettings->viewportHeight = m_Context.platform->GetWindow()->GetFramebufferHeight();
            m_RendererSettings->api = (RendererAPI::API)settings.api;

            // Vulkan presents on a surface of the window : the renderer asks the window for it, it does not know it
            if(m_RendererSettings->api == RendererAPI::API::Vulkan)
            {
                Platform::IWindow* window = m_Context.platform->GetWindow();
                Rendering::WindowHooks hooks;
                hooks.vulkanInstanceExtensions = [window] { return window->GetVulkanInstanceExtensions(); };
                hooks.createVulkanSurface = [window](void* instance) { return window->CreateVulkanSurface(instance); };
                hooks.width = m_RendererSettings->viewportWidth;
                hooks.height = m_RendererSettings->viewportHeight;
                hooks.vsync = settings.vsync != 0;
                hooks.vsyncWanted = [] { return cvVsync.Get(); };
                hooks.validation = cvVulkanValidation.Get() || std::getenv("SHARD_VULKAN_VALIDATION") != nullptr;
                Rendering::RendererAPI::SetWindowHooks(hooks);
            }

            m_Context.renderer->Init(m_RendererSettings);
            // The probe manager lives on the render thread : what it needs from the world or the resources comes through the
            // main thread's queue, and its answers go straight back to it
            Rendering::ProbeManager* probeManager = m_Context.renderer->GetProbeManager().get();
            probeManager->onBakeFinished = [this](uint64_t key, const Filesystem::Path& file) {
                PostToMainThread([this, key, file] {
                    if(auto* volume = Rendering::FindProbeVolume(*m_Context.worldManager, key))
                        volume->OnBakeFinished(file);
                });
            };
            probeManager->requestBakedData = [this, probeManager](const std::string& path) {
                PostToMainThread([this, probeManager, path] {
                    probeManager->ProvideBakedData(path, m_Context.resourcesManager->Get<Rendering::ProbeBakeData>(Core::Resources::AssetKind::ProbeBake, path));
                });
            };
            probeManager->releaseBakedData = [this](const std::string& path) {
                PostToMainThread([this, path] { m_Context.resourcesManager->Unload(Core::Resources::AssetKind::ProbeBake, path); });
            };
            // The same for the lightmaps : a bake hands its result to the world on the main thread, and the pages the world names
            // are loaded there
            Rendering::LightmapManager* lightmaps = m_Context.renderer->GetLightmapManager().get();
            lightmaps->onBakeFinished = [this](const Rendering::LightmapBakeResult& result) {
                PostToMainThread([this, result] {
                    if(Worlds::World* world = m_Context.worldManager ? m_Context.worldManager->GetWorldAt(0) : nullptr)
                        world->Ext<Rendering::RenderWorldData>().OnLightmapsBaked(*world, result);
                });
            };
            lightmaps->requestData = [this, lightmaps](const std::string& path) {
                PostToMainThread([this, lightmaps, path] {
                    lightmaps->ProvideData(path, m_Context.resourcesManager->Get<Rendering::LightmapData>(Core::Resources::AssetKind::Lightmap, path));
                });
            };
            lightmaps->releaseData = [this](const std::string& path) {
                PostToMainThread([this, path] { m_Context.resourcesManager->Unload(Core::Resources::AssetKind::Lightmap, path); });
            };
            m_Context.renderer->SetOcclusionCullingEnabled(m_Context.currentProject->GetRenderingSettings()->occlusionCulling);
            m_Context.renderer->SetMultisampleCount(m_Context.currentProject->GetRenderingSettings()->msaaSamples);
            m_Context.renderer->SetShadowSettings(m_Context.currentProject->GetRenderingSettings()->shadows);
            m_Context.renderer->SetRenderPipeline(m_Context.currentProject->GetRenderingSettings()->renderPipeline);
            m_Context.platform->CreateInput();

            // After the input's own listener (registered by CreateInput) : the input state is up to date when a handler runs
            m_InputEventListener = GetWindow()->AddEventListener([this](const Platform::OSEvent& event) {
                Events::TranslateInputEvent(event, *m_Context.eventDispatcher);
            });

            m_Context.timeManager->Init(physicsSettings->fixedTimeStep, physicsSettings->maxAccumulatedTime);

            Platform::SystemInfos infos = GetWindow()->GetSystemInfos();

            DEBUG_INFO("===== System infos =====");
            DEBUG_INFO("GPU Vendor : " + infos.gpu_vendor);
            DEBUG_INFO("GPU Renderer : " + infos.gpu_renderer);
            DEBUG_INFO("Driver Version : " + infos.driver_version);
            DEBUG_INFO("Using Host : " + std::string(infos.windowHost == Platform::WindowHost::QT ? "QT" : infos.windowHost == Platform::WindowHost::Headless ? "none (headless)" : "SDL3") + " with version : " + infos.windowHostVersion);
            DEBUG_INFO("CPU : " + infos.cpuBrand + " (" + std::to_string(infos.cpuLogicalCores) + " threads)");
            DEBUG_INFO("RAM : " + std::to_string(infos.totalRamMB) + " MB");

            DEBUG_INFO("Monitors : ");
            for(int i = 0; i < infos.connectedMonitorsCount; i++){
                DEBUG_INFO("    Monitor : "+ std::to_string(i) + " : width = " + std::to_string(infos.monitors[i].width) + " px, height = "+ std::to_string(infos.monitors[i].height) + " px, refreshRate = "+std::to_string(infos.monitors[i].refreshRate)+" hz");
            }

            std::string startWorld = settings.startWorld;
            if(startWorld.empty() && m_Context.currentProject->GetBuildSettings()->buildIndex.size() > 0)
                startWorld = m_Context.currentProject->GetBuildSettings()->buildIndex[0].full;

            // The engine is up: the plugins start running, then the first world loads (they hear about it)
            {
                Plugins::PluginManager& plugins = Plugins::PluginManager::Get();
                Plugins::PluginManager::Callbacks callbacks;
                callbacks.hasLoadedWorlds = [this]{ return m_Context.worldManager->GetLoadedWorldCount() > 0; };
                // A plugin is reloaded with the worlds put aside : their components have code in its library. What was edited and not saved is kept
                // in the files the way play mode keeps it.
                auto suspended = std::make_shared<std::vector<std::string>>();
                callbacks.suspendWorlds = [this, suspended](std::string& error){
                    suspended->clear();
                    for(int i = 0; i < m_Context.worldManager->GetLoadedWorldCount(); i++){
                        Worlds::World* world = m_Context.worldManager->GetWorldAt(i);
                        auto info = world ? m_Context.assetIDManager->GetAssetFromID(world->GetAssetID()) : nullptr;
                        if(!info){ error = "a loaded world is not in the asset database"; suspended->clear(); return false; }
                        if(world->IsDirty() && !m_EngineSettings.readOnly){ error = "'" + info->baseInfos.nameInProject + "' has unsaved changes : save it first"; suspended->clear(); return false; }
                        suspended->push_back(info->baseInfos.nameInProject);
                    }
                    while(m_Context.worldManager->GetLoadedWorldCount() > 0){
                        const int last = m_Context.worldManager->GetLoadedWorldCount() - 1;
                        Worlds::World* world = m_Context.worldManager->GetWorldAt(last);
                        auto info = m_Context.assetIDManager->GetAssetFromID(world->GetAssetID());
                        const std::string name = info ? info->baseInfos.nameInProject : "";
                        m_Context.worldManager->UnloadWorld(last);
                        if(!name.empty())
                            m_Context.resourcesManager->Unload(Core::Resources::AssetKind::World, name);
                    }
                    m_Context.renderer->ClearPassesContent();
                    m_Context.objIDManager->Reset();
                    return true;
                };
                callbacks.resumeWorlds = [this, suspended]{
                    for(const std::string& name : *suspended){
                        auto world = m_Context.resourcesManager->Get<Worlds::World>(Core::Resources::AssetKind::World, name);
                        if(world)
                            m_Context.worldManager->LoadWorld(world);
                        else
                            DEBUG_ERROR("Plugins : could not load ", name, " back after the reload");
                    }
                    suspended->clear();
                };
                plugins.SetCallbacks(std::move(callbacks));
                plugins.OnEngineInitialized();
            }

            if(!startWorld.empty() && !Plugins::PluginManager::Get().BeforeWorldLoad(startWorld)){
                DEBUG_WARNING("The default world was not loaded because a plugin refused it");
                startWorld.clear();
            }

            if(!startWorld.empty()){
                DEBUG_LOG("Loading default world : "+startWorld);

                // Parallelizes texture/mesh decode across worker threads and keeps pumping window
                // events (+ drawing a splash frame, on platforms that support one) while it waits, so
                // a heavy default world doesn't leave the window looking frozen at boot.
                Platform::IWindow* window = GetWindow();
                GetWorldManager()->LoadWorldBlocking(startWorld, [window](float progress){
                    window->PollEvents();
                    window->DrawLoadingFrame(progress);
                });
                Plugins::PluginManager::Get().AfterWorldLoad(startWorld);
            }

            // Everything above ran on this thread, which owns the graphics context : from here on the render thread does
            StartRenderThread();

            StartHotReload();

            if(!settings.scriptFile.empty())
                LoadScript(settings.scriptFile);

        }

        uint64_t EngineInstance::GetSimulationSeed() const
        {
            return Determinism::Seed();
        }

        Random EngineInstance::MakeRandomStream(std::string_view name) const
        {
            return Determinism::MakeStream(name);
        }

        ThreadPool* EngineInstance::GetThreadPool(PoolKind kind) const
        {
            return m_Context.threadPools ? &m_Context.threadPools->Get(kind) : nullptr;
        }

        void EngineInstance::InitSystems()
        {
            // What each module adds to the world (its components, what it keeps on a world) : the world itself
            // knows none of them.
            Rendering::RegisterRenderingModule();
            Physics::RegisterPhysicsModule();
            Audio::RegisterAudioModule();

            JobSystemDesc jobDesc;
            jobDesc.workerCount = Platform::WorkerThreadCount();
            // SHARD_JOB_THREADS pins the number of job threads (ShardHeadless --check-plugins runs the same simulation with different counts)
            if(const auto pinned = Platform::GetEnv("SHARD_JOB_THREADS")){
                try { jobDesc.workerCount = static_cast<unsigned>(std::max(1, std::stoi(*pinned))); } catch(...) {}
            }
            jobDesc.onWorkerStart = [](unsigned index){ Platform::SetCurrentThreadName("Shard Job " + std::to_string(index)); };
            m_Context.jobSystem = new JobSystem(jobDesc);
            m_Context.threadPools = new ThreadPools([](PoolKind kind, const std::string& name, unsigned index){
                Platform::SetCurrentThreadName("Shard " + name + " " + std::to_string(index));
                if(kind == PoolKind::Audio)
                    Platform::SetCurrentThreadPriority(Platform::ThreadPriority::High);   // a late audio buffer is heard
            });
            DEBUG_INFO("Job system : " + std::to_string(m_Context.jobSystem->ThreadCount()) + " threads");

            m_Context.frameAllocator = new FrameAllocator(size_t(cvFrameKB.Get()) * 1024, 2, "FrameAllocator");
            cvJobThreads.ForceSet(static_cast<int>(m_Context.jobSystem->ThreadCount()));

            m_Context.renderer = new Rendering::Renderer();
            m_Context.cameraManager = new Rendering::CameraManager();

            // The renderer knows no camera : whoever owns them follows the viewport
            m_Context.renderer->onViewportResized = [this](int width, int height) { m_Context.cameraManager->UpdateSize(width, height); };

            m_Context.assetIDManager = new Filesystem::AssetIDManager();
            m_Context.fileManager = new Filesystem::FileManager(*m_Context.assetIDManager);
            m_Context.resourcesManager = new Resources::ResourcesManager(*m_Context.fileManager, *m_Context.assetIDManager);

            // Settings files next to the executable : engine.cfg (defaults shipped with the engine) and user.cfg
            // (what the user changed and saved). The project's own project.cfg is read once the project is known.
            LoadConfigLayer(ConfigLayer::Engine, ConfigPath(m_Context.fileManager->GetCurrentExecutablePath(), "engine.cfg"));
            LoadConfigLayer(ConfigLayer::User, ConfigPath(m_Context.fileManager->GetCurrentExecutablePath(), "user.cfg"));

            m_Context.objIDManager = new ObjectIDManager();

            // The entities (created before the worlds : an actor owns one) and the systems that run on them each frame
            m_Context.ecs = new Ecs::Registry();
            m_Context.scheduler = new Ecs::Scheduler();

            m_Context.worldManager = new Worlds::WorldManager();
            m_Context.worldManager->onAllWorldsUnloaded = [this]{ m_Context.renderer->ClearPassesContent(); };
            Worlds::RegisterWorldAssetKind(*m_Context.resourcesManager);

            m_Context.eventDispatcher = new Events::EventDispatcher();

            m_Context.audioManager = new Audio::AudioManager();
            m_Context.audioIDManager = new Audio::AudioIDManager();

            m_Context.physicsManager = new Physics::PhysicsManager();

            m_Context.timeManager = new Time::TimeManager();

            m_Context.profiler = new Debugging::Profiler();
            Debugging::Profiler::SetActive(m_Context.profiler);

            m_Context.profiler->AddStatsProvider([this](Debugging::MinimalStatistics& stats)
            {
                stats.frameTimeMs = m_Context.timeManager->GetDeltaTime() * 1000;
                stats.fps = 1000 / stats.frameTimeMs;

                auto* world = m_Context.worldManager->GetWorldAt(0);
                stats.actors = world ? world->transforms.size() : 0;
            });

            m_Context.profiler->AddStatsProvider([](Debugging::MinimalStatistics& stats)
            {
                stats.gpuMemoryMB = Platform::ProcessGpuMemoryUsage() / (1024.0f * 1024.0f);
                stats.ramUsageMB = Platform::ProcessMemoryUsage() / (1024.0f * 1024.0f);
            });

            RegisterEngineSystems();
        }

        void EngineInstance::RegisterEngineSystems()
        {
            using namespace Ecs;
            Scheduler& scheduler = *m_Context.scheduler;

            // What the engine's managers do each frame, expressed as systems of the phases. They touch the managers (not
            // components), which the read / write declaration can't express, hence Exclusive : they run alone, in this order.
            auto add = [&scheduler](const char* name, Phase phase, SystemFn fn, bool mainThread = false)
            {
                SystemDesc desc;
                desc.name = name;
                desc.phase = phase;
                desc.exclusive = true;
                desc.mainThread = mainThread;
                desc.run = std::move(fn);
                scheduler.Add(std::move(desc));
            };

            add("Input.Poll", Phase::Input, [this](SystemContext&){ PollInput(); });

            add("Worlds.Update", Phase::PreSim, [this](SystemContext&){ UpdateWorlds(); });

            // Physics runs at a fixed rate, decoupled from the render rate : a frame runs as many steps as the elapsed real
            // time paid for (0 when rendering outpaces the simulation, several when it lags behind).
            add("Scripts.FixedTick", Phase::Fixed, [this](SystemContext&){ if(m_PlayMode) TickScripts(ScriptTick::Fixed); });
            add("Physics.Step", Phase::Fixed, [this](SystemContext& c){ if(m_PlayMode) StepPhysics(c.fixedStep, c.fixedDeltaTime); });

            add("Audio.Tick", Phase::Update, [this](SystemContext&){ if(m_PlayMode) TickAudio(); });
            add("Scripts.Tick", Phase::Update, [this](SystemContext&){
                if(m_PlayMode) TickScripts(ScriptTick::Update);
                else m_Context.worldManager->EditTick();        // the scripts that asked to be seen working in the editor
            });

            // Loads the pictures the reflection probes reflect and advances the bake that is running : not gated by the play mode, a probe is
            // baked from the editor
            add("ReflectionProbes.Tick", Phase::Update, [](SystemContext&){ Objects::Components::ReflectionProbe::TickAll(); }, true);

            // After the scripts moved things : kinematic targets go to Jolt, dynamic results come back to the transforms
            add("Physics.Sync", Phase::Late, [this](SystemContext& c){ SyncPhysicsBodies(c.fixedDeltaTime); });
            add("Scripts.LateTick", Phase::Late, [this](SystemContext&){ if(m_PlayMode) TickScripts(ScriptTick::Late); });

            auto extractor = std::make_shared<Rendering::RenderExtractor>();
            auto poseOffsets = std::make_shared<Rendering::RenderExtractor::PoseOffsets>();
            extractor->SetPoseOffsets(poseOffsets.get());
            // What the plugins put on screen (shard.render) joins each snapshot
            extractor->AddSnapshotProvider(&Plugins::CAbi::ExtractPluginRender);
            add("Render.Extract", Phase::Extract, [this, extractor, poseOffsets](SystemContext& c){
                extractor->SetRebaseDistance(double(cvRebaseDistance.Get()));
                // The pacing : the simulation does not get further ahead of the drawn frames than the limit
                {
                    SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Waiting);
                    Rendering::RenderThread::Get().WaitForFrameSlot();
                }

                SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Rendering);

                // Where each moving body is drawn this frame : the frame sits `alpha` of the way from the last step to the next
                poseOffsets->clear();
                if(m_PlayMode && cvPhysicsInterpolate.Get() && c.fixedDeltaTime > 0.0f)
                {
                    const float alpha = m_Context.timeManager->GetFixedStepAlpha();
                    for(int i = 0; i < m_Context.worldManager->GetLoadedWorldCount(); i++)
                    {
                        Worlds::World* world = m_Context.worldManager->GetWorldAt(i);
                        if(!world)
                            continue;
                        for(auto& [id, body] : world->Ext<PhysicsWorldData>().bodies)
                        {
                            glm::mat4 offset;
                            if(body && body->Active() && body->parent && body->GetInterpolationOffset(alpha, offset))
                                poseOffsets->emplace(body->parent->GetID().GetAsInt(), offset);
                        }
                    }
                }

                extractor->Extract(*m_Context.renderer, *m_Context.worldManager, *m_Context.cameraManager, m_Context.jobSystem, c.frame);
            }, true);
            CVarRegistry::Global().RegisterCommand("render.snapshot", "render.snapshot : what the last Extract stage copied out of the simulation for the renderer",
                [this, extractor](const std::vector<std::string>&) {
                    const Rendering::RenderSnapshot& snapshot = m_Context.renderer->GetSnapshot();
                    const Rendering::RenderExtractor::Stats& stats = extractor->GetLastStats();
                    std::string text = "snapshot of frame " + std::to_string(snapshot.frame) + " (" + std::to_string(m_Context.renderer->GetSnapshotBuffer().PublishedCount()) + " published), viewport " +
                                       std::to_string(snapshot.viewportWidth) + "x" + std::to_string(snapshot.viewportHeight) + "\n";
                    text += std::string("  camera : ") + (snapshot.camera.valid ? "yes" : "none") + "\n";
                    text += "  meshes : " + std::to_string(stats.meshes) + " (" + std::to_string(stats.meshesChanged) + " changed" + (stats.parallel ? ", extracted on the job system" : "") + ")\n";
                    text += "  lights : " + std::to_string(stats.lights) + ", probe volumes : " + std::to_string(stats.probeVolumes) + ", sky : " + (snapshot.environment.skybox ? "yes" : "none");
                    return CommandResult{true, text};
                });

            add("Render.Frame", Phase::Extract, [this](SystemContext&){ RenderFrame(); }, true);

            RegisterConsoleCommands();

            CVarRegistry::Global().RegisterCommand("asset.import", "asset.import <file> [output folder] : converts a file with the importer a plugin brought for its extension (the output goes to the project's resources by default)",
                [this](const std::vector<std::string>& args) {
                    if(args.empty()){
                        std::string known;
                        for(const std::string& e : Filesystem::AssetImporters::Get().Extensions())
                            known += (known.empty() ? "" : " ") + e;
                        return CommandResult{false, "usage : asset.import <file> [output folder]\nimporters take : " + (known.empty() ? std::string("(none)") : known)};
                    }
                    std::string output = args.size() > 1 ? args[1] : std::string();
                    if(output.empty() && m_Context.currentProject)
                        output = m_Context.currentProject->GetProjectResourcesPath().GetNativePath();
                    if(output.empty())
                        return CommandResult{false, "no project : give an output folder"};
                    const Filesystem::ImportResult result = Filesystem::AssetImporters::Get().Import(args[0], output);
                    std::string text = result.message;
                    for(const std::string& file : result.outputs)
                        text += (text.empty() ? "" : "\n") + std::string("wrote ") + file;
                    return CommandResult{result.ok, text.empty() ? std::string("imported") : text};
                });

            CVarRegistry::Global().RegisterCommand("render.features", "render.features : the render features plugins added, and the render pipelines that can be picked",
                [this](const std::vector<std::string>&) {
                    std::string text = "features :";
                    for(const std::string& name : m_Context.renderer->GetRenderFeatureNames())
                        text += " " + name;
                    text += "\npipelines :";
                    for(const Rendering::RenderPipelineInfo& info : Rendering::RenderPipelineRegistry::Get().List())
                        text += " " + info.name;
                    return CommandResult{true, text};
                });

            CVarRegistry::Global().RegisterCommand("render.thread", "render.thread : what the render thread does (frames, time per frame, how long the simulation waited for it)",
                [](const std::vector<std::string>& args) {
                    Rendering::RenderThread& thread = Rendering::RenderThread::Get();
                    if(!args.empty() && args[0] == "reset")
                    {
                        thread.ResetStats();
                        return CommandResult{true, "statistics reset"};
                    }
                    if(!thread.IsRunning())
                        return CommandResult{true, "no render thread : drawing on the main thread (render.thread 0, or not started yet)"};
                    const Rendering::RenderThread::Stats s = thread.GetStats();
                    char text[512];
                    std::snprintf(text, sizeof(text),
                        "render thread running, %d frame(s) in flight allowed\n  frames : %llu submitted, %llu drawn\n"
                        "  render thread : %.2f ms per frame, busy %.0f%% of the time\n"
                        "  simulation : waited %.2f ms per frame for a slot, %.2f ms parked in calls to the render thread",
                        thread.MaxFramesInFlight(), (unsigned long long)s.framesSubmitted, (unsigned long long)s.framesFinished,
                        s.averageFrameMs, s.busyFraction * 100.0, s.averageWaitMs, s.averageInvokeMs);
                    std::string result = text;
                    if(thread.WrongThreadCount() > 0)
                    {
                        result += "\n  !! " + std::to_string(thread.WrongThreadCount()) + " call(s) used the graphics API from the wrong thread :";
                        for(const std::string& place : thread.WrongThreadPlaces())
                            result += " " + place;
                    }
                    return CommandResult{true, result};
                });

            // Console : the hash of the simulation state (what two runs compare to say they stayed identical, see simulation_state.hpp)
            CVarRegistry::Global().RegisterCommand("sim.hash", "sim.hash : the hash of the simulation state (transforms, physics bodies, scripts' fields), and the seed of the run",
                [this](const std::vector<std::string>&) {
                    return CommandResult{true, HashSimulationState(*this).Describe() + ", seed " + std::to_string(GetSimulationSeed()) +
                                               (m_EngineSettings.deterministic ? ", deterministic" : ", not deterministic")};
                });

            // Console : what runs each frame and how long it takes, what the registry holds
            CVarRegistry::Global().RegisterCommand("ecs.systems", "ecs.systems : the systems of each phase, in start order, with their last run time",
                [this](const std::vector<std::string>&) {
                    return CommandResult{true, m_Context.scheduler ? m_Context.scheduler->Describe() : std::string("no scheduler")};
                });
            CVarRegistry::Global().RegisterCommand("ecs.stats", "ecs.stats : entities, archetypes and chunk memory ; with 'archetypes', one line per archetype",
                [this](const std::vector<std::string>& args) {
                    if(!m_Context.ecs)
                        return CommandResult{false, "no registry"};
                    const Ecs::Registry::Stats stats = m_Context.ecs->GetStats();
                    std::string text = std::to_string(stats.entities) + " entities, " + std::to_string(stats.archetypes) + " archetypes, " +
                                       std::to_string(stats.chunks) + " chunks (" + std::to_string(stats.chunkBytes / 1024) + " KB), " +
                                       std::to_string(stats.pendingDestroys) + " destroys pending";
                    if(!args.empty() && args[0] == "archetypes")
                        text += "\n" + m_Context.ecs->Describe();
                    return CommandResult{true, text};
                });
        }

        Rendering::IRenderContext *EngineInstance::GetRenderContext() const
        {
            return m_Context.renderer;
        }

        Platform::IWindow *EngineInstance::GetWindow() const
        {
            return m_Context.platform->GetWindow();
        }

        Platform::IInput *EngineInstance::GetInputManager() const
        {
            return m_Context.platform->GetInput();
        }

        Projects::BuildSettings *EngineInstance::GetBuildSettings() const
        {
            return m_Context.currentProject ? m_Context.currentProject->GetBuildSettings() : nullptr;
        }

        void EngineInstance::SetPlayMode(bool on)
        {
            if(on && !Plugins::PluginManager::Get().BeforePlay())
                return;

            m_PlayMode = on;

            if(m_PlayMode){

                // Play is reachable with no world loaded (editor Play button, `play on`)
                if(!m_EngineSettings.readOnly && m_Context.worldManager->GetWorldAt(0))
                    m_Context.worldManager->GetWorldAt(0)->Serialize(m_Context.worldManager->GetWorldAt(0)->GetPath());

                for(int i = 0; i < m_Context.worldManager->GetLoadedWorldCount(); i++){
                    m_Context.worldManager->GetWorldAt(i)->Play();
                }

                auto* world = m_Context.worldManager->GetWorldAt(0);
                if (world && !world->Ext<Rendering::RenderWorldData>().cameras.empty()){

                    auto cameraEntry = world->Ext<Rendering::RenderWorldData>().cameras.begin();
                    auto camera = cameraEntry->second;
                    if (!camera) {
                        DEBUG_ERROR("First camera is not valid for world : ", world->GetName());
                        return;
                    }

                    auto parent = camera->parent;
                    if (!parent) {
                        DEBUG_ERROR("First camera's parent is not valid for world : ", world->GetName());
                        return;
                    }

                    m_Context.cameraManager->SetActiveCamera(parent->GetID());
                }

                m_ActivateAllPhysics = true;
                Plugins::PluginManager::Get().AfterPlay();
            }
            else{
                
                for(int i = 0; i < m_Context.worldManager->GetLoadedWorldCount(); i++){
                    m_Context.worldManager->GetWorldAt(i)->Stop();
                }

                // A game may have hidden and captured the cursor (a first person controller) : whoever stops the play session
                // (button, menu, F1, console) gets it back
                if(Platform::IInput* input = GetInputManager())
                    input->SetCursorVisibility(Platform::CursorVisibility::Visible);

                m_ReloadCurrentWorld = true;
            }
        }

        bool EngineInstance::ShouldEnd()
        {
            return m_Context.platform && m_Context.platform->GetWindow()->ShouldClose();
        }

        void EngineInstance::Destroy()
        {
            if(m_HotReloader){
                m_HotReloader->Stop();
                delete m_HotReloader;
                m_HotReloader = nullptr;
            }

            // The graphics context comes back to this thread before anything is torn down
            StopRenderThread();

            if(!m_EngineSettings.readOnly)
                m_Context.currentProject->Shutdown(*m_Context.assetIDManager);
            m_Context.profiler->Shutdown();
            Debugging::Profiler::SetActive(nullptr);
            if(m_InputEventListener != 0)
            {
                GetWindow()->RemoveEventListener(m_InputEventListener);
                m_InputEventListener = 0;
            }
            m_Context.platform->GetInput()->Shutdown();
            m_Context.audioManager->Shutdown();
            m_Context.physicsManager->Shutdown();
            m_Context.worldManager->UnloadAllWorlds();
            // The plugins stop before the systems and the renderer they registered into go away
            Plugins::PluginManager::Get().OnEngineDestroying();
            if(gAssetTypeListener){
                Plugins::PluginManager::Get().Extensions().Unlisten(gAssetTypeListener);
                gAssetTypeListener = 0;
            }
            if(gAssetImporterListener){
                Plugins::PluginManager::Get().Extensions().Unlisten(gAssetImporterListener);
                gAssetImporterListener = 0;
            }
            m_Context.renderer->Shutdown();
            m_Context.platform->GetWindow()->Destroy();

            CVarRegistry::Global().UnregisterCommand("render.thread");
            CVarRegistry::Global().UnregisterCommand("asset.import");
            CVarRegistry::Global().UnregisterCommand("render.features");

            // Last : every system that submitted work is shut down, and every JobGroup has been waited on. The pools'
            // queued tasks are run to the end first (they may use the job system), then the job system goes.
            CVarRegistry::Global().UnregisterCommand("sim.hash");
            CVarRegistry::Global().UnregisterCommand("ecs.systems");
            CVarRegistry::Global().UnregisterCommand("ecs.stats");
            delete m_Context.scheduler;
            m_Context.scheduler = nullptr;
            delete m_Context.ecs;
            m_Context.ecs = nullptr;
            delete m_Context.threadPools;
            m_Context.threadPools = nullptr;
            delete m_Context.jobSystem;
            m_Context.jobSystem = nullptr;
            delete m_Context.frameAllocator;
            m_Context.frameAllocator = nullptr;

            // The settings the user changed (Archive variables that differ from their default) for the next run
            if(!m_EngineSettings.readOnly && m_Context.fileManager)
            {
                const std::string userConfig = ConfigPath(m_Context.fileManager->GetCurrentExecutablePath(), "user.cfg");
                CVarRegistry& registry = CVarRegistry::Global();
                const std::string archive = registry.ArchiveText();
                const bool hasSomethingToSave = archive.find('\n') + 1 < archive.size();   // more than the header line
                if(hasSomethingToSave || Filesystem::Path(userConfig).Exists())
                    registry.SaveArchive(userConfig);
            }

            // Anything an engine allocator allocated and did not give back
            const std::string leaks = MemoryTracker::Get().Report();
            if(!leaks.empty())
                DEBUG_WARNING("Memory not released at shutdown : " + leaks);
        }

        bool EngineInstance::Run() {

            const auto frameStart = std::chrono::steady_clock::now();

            RunScriptedCommands();
            m_FrameCount++;
            Plugins::PluginManager::Get().BeginFrame(m_FrameCount);

            // Measured first thing in the frame so every system below works off the same delta, and so
            // the time the previous frame actually took is what decides how much simulation is owed.
            // A deterministic run does not look at the clock : every frame is exactly one fixed step, so the simulation does
            // the same thing whatever the machine is doing meanwhile
            m_Context.timeManager->Tick(m_EngineSettings.deterministic ? m_Context.timeManager->GetFixedDeltaTime() : cvFrameDt.Get());

            // New frame of scratch memory : what was allocated two frames ago is gone, last frame's is still readable
            m_Context.frameAllocator->NextFrame();

            Ecs::Scheduler::FrameTiming timing;
            timing.deltaTime = m_Context.timeManager->GetDeltaTime();
            timing.fixedDeltaTime = m_Context.timeManager->GetFixedDeltaTime();
            if(m_PlayMode)
                timing.fixedSteps = m_Context.timeManager->ConsumeFixedSteps();
            else
                // Nothing is simulating, so don't bank time that would be replayed as a burst of steps
                // the moment play mode starts.
                m_Context.timeManager->ResetAccumulator();

            // Input -> PreSim -> Fixed x N -> PostPhysics -> Update -> Late -> Extract (see RegisterEngineSystems)
            m_Context.scheduler->RunFrame(*m_Context.ecs, m_Context.jobSystem, timing);

            Present();

            if(const int maxFPS = cvEngineMaxFPS.Get(); maxFPS > 0)
                HoldUntil(frameStart, maxFPS);

            // The duration of this frame, which is what the categories sampled above add up to. The simulation's delta is the
            // previous frame's (it is read at the start of this one), so it would be shown against the wrong frame's breakdown.
            m_Context.profiler->EndFrameSampling(std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - frameStart).count(), m_FrameCount);

            Plugins::PluginManager::Get().EndFrame(m_FrameCount);

            return true;
        }

        void EngineInstance::StepPhysics(int step, float fixedDeltaTime)
        {
            SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Physics);

            // Intermediate steps need the bodies re-synced in between (kinematic targets pushed to Jolt,
            // dynamic results read back); the last step's results are picked up by the Late phase's Physics.Sync,
            // before rendering.
            if(step > 0)
                m_Context.physicsManager->TickBodies(fixedDeltaTime);

            // The fields move with their actors (and scripts change them) : read again for every step
            m_Context.physicsManager->CollectGravityFields();
            m_Context.physicsManager->StepSimulation(fixedDeltaTime, m_ActivateAllPhysics);

            // Cleared here rather than once per frame: the first frames of play mode can
            // run zero steps, and the wake-up has to survive until a step actually happens.
            m_ActivateAllPhysics = false;
        }

        void EngineInstance::TickAudio()
        {
            SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Audio);
            m_Context.audioManager->Tick();
        }

        void EngineInstance::TickScripts(ScriptTick kind)
        {
            SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Scripting);
            switch(kind)
            {
                case ScriptTick::Fixed:  m_Context.worldManager->FixedTick(); break;
                case ScriptTick::Update: m_Context.worldManager->Tick();      break;
                case ScriptTick::Late:   m_Context.worldManager->LateTick();  break;
            }
        }

        void EngineInstance::UpdateWorlds()
        {
            m_Context.worldManager->PumpAsyncLoad();

            if(m_ReloadCurrentWorld && !GetWorldManager()->GetWorldAt(0))
                m_ReloadCurrentWorld = false;

            if(m_ReloadCurrentWorld)
            {
                // A world made in memory (the editor's New World) has no file to be reloaded from : it is left as it is
                auto info = GetAssetIDManager()->GetAssetFromID(GetWorldManager()->GetWorldAt(0)->GetAssetID());
                if(!info || info->baseInfos.nameInProject.empty())
                {
                    DEBUG_WARNING("The current world is not saved as an asset : nothing to reload it from");
                    m_ReloadCurrentWorld = false;
                    return;
                }
                std::string worldNameInProject = info->baseInfos.nameInProject;
                GetWorldManager()->UnloadWorld(0);
                GetResourcesManager()->Unload(Core::Resources::AssetKind::World, worldNameInProject);
                GetRenderer()->ClearPassesContent();
                GetObjectIDManager()->Reset();
                auto world = GetResourcesManager()->Get<Worlds::World>(Core::Resources::AssetKind::World, worldNameInProject);
                if(world)
                {
                    GetWorldManager()->LoadWorld(world);
                    GetResourcesManager()->CollectUnused();
                }
                else
                    DEBUG_ERROR("Error re-loading world !");
                m_ReloadCurrentWorld = false;
            }
        }

        void EngineInstance::SyncPhysicsBodies(float fixedDeltaTime)
        {
            SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Physics);
            m_Context.physicsManager->TickBodies(fixedDeltaTime);
        }

        void EngineInstance::RenderFrame()
        {
            // Handed to the render thread : the frame draws from the snapshot Extract just published, while this thread
            // goes on with the next one. Without a render thread it simply runs here.
            Rendering::Renderer* renderer = m_Context.renderer;
            Debugging::Profiler* profiler = m_Context.profiler;
            const uint64_t frame = m_FrameCount;
            if(Platform::IWindow* window = GetWindow())
                window->PrepareFrame();
            Rendering::RenderThread::Get().SubmitFrame([renderer, profiler, frame] {
                // Only this thread touches it : the start of the previous frame is what the limit is counted from
                static std::chrono::steady_clock::time_point lastStart = std::chrono::steady_clock::now();
                if(const int maxFPS = cvMaxFPS.Get(); maxFPS > 0)
                    HoldUntil(lastStart, maxFPS);
                lastStart = std::chrono::steady_clock::now();

                // The render thread's samples from here on (this frame, its UI, its swap) belong to the simulation's frame `frame`
                if(profiler)
                    profiler->BeginRenderFrame(frame);
                SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Rendering);
                renderer->Render();
            });
        }

        void EngineInstance::StartRenderThread()
        {
            if(!cvRenderThread.Get())
                return;

            Platform::IWindow* window = GetWindow();
            if(!window || m_RendererSettings->api != RendererAPI::API::OpenGL)
                return;

            Rendering::RenderThread& thread = Rendering::RenderThread::Get();
            thread.SetMaxFramesInFlight(cvFramesInFlight.Get());

            const bool started = thread.Start(
                [window] { window->MakeContextCurrent(false); },
                [window] {
                    Platform::SetCurrentThreadName("Shard Render");
                    Debugging::Profiler::MarkRenderThread();
                    return window->MakeContextCurrent(true);
                });

            if(started)
                DEBUG_INFO("Render thread started (" + std::to_string(thread.MaxFramesInFlight()) + " frame(s) in flight)");
            else
                DEBUG_WARNING("The render thread could not take the graphics context : drawing on the main thread");
        }

        void EngineInstance::StopRenderThread()
        {
            Rendering::RenderThread& thread = Rendering::RenderThread::Get();
            if(!thread.IsRunning())
                return;

            Platform::IWindow* window = GetWindow();
            thread.Stop(
                [window] {
                    Debugging::Profiler::UnmarkRenderThread();
                    window->MakeContextCurrent(false);
                },
                [window] { return window->MakeContextCurrent(true); });
            thread.onFrameFinished = nullptr;
        }

        void EngineInstance::LoadScript(const std::string& path)
        {
            std::ifstream file(path);
            if(!file)
            {
                DEBUG_ERROR("Script not found : " + path);
                return;
            }

            std::string line;
            while(std::getline(file, line))
            {
                if(!line.empty() && line.back() == '\r')
                    line.pop_back();
                size_t start = line.find_first_not_of(" \t");
                if(start == std::string::npos || line[start] == '#')
                    continue;
                size_t space = line.find(' ', start);
                if(space == std::string::npos)
                    continue;
                try
                {
                    m_Script.push_back({std::stoull(line.substr(start, space - start)), line.substr(space + 1)});
                }
                catch(...) { DEBUG_WARNING("Script line ignored : " + line); }
            }
            std::stable_sort(m_Script.begin(), m_Script.end(), [](const ScriptedCommand& a, const ScriptedCommand& b) { return a.frame < b.frame; });
            DEBUG_INFO("Script : " + std::to_string(m_Script.size()) + " command(s) from " + path);
        }

        void EngineInstance::RunScriptedCommands()
        {
            while(m_ScriptNext < m_Script.size() && m_Script[m_ScriptNext].frame <= m_FrameCount)
            {
                const std::string& line = m_Script[m_ScriptNext++].line;
                const CommandResult result = CVarRegistry::Global().Execute(line);
                DEBUG_INFO("Script @" + std::to_string(m_FrameCount) + " > " + line + (result.ok ? " : " : " FAILED : ") + result.output);
            }
        }

        void EngineInstance::PostToMainThread(std::function<void()> work)
        {
            std::lock_guard<std::mutex> lock(m_MainQueueMutex);
            m_MainQueue.push_back(std::move(work));
        }

        void EngineInstance::RunMainThreadQueue()
        {
            std::vector<std::function<void()>> work;
            {
                std::lock_guard<std::mutex> lock(m_MainQueueMutex);
                work.swap(m_MainQueue);
            }
            for(auto& fn : work)
                fn();
        }

        void EngineInstance::ReloadWorldFromDisk(const std::string& nameInProject)
        {
            auto nameOfWorld = [this](int index) -> std::string {
                Worlds::World* world = m_Context.worldManager->GetWorldAt(index);
                auto info = world ? m_Context.assetIDManager->GetAssetFromID(world->GetAssetID()) : nullptr;
                return info ? info->baseInfos.nameInProject : "";
            };

            for(int i = 0; i < m_Context.worldManager->GetLoadedWorldCount(); i++){
                if(nameOfWorld(i) != nameInProject)
                    continue;

                if(m_PlayMode){
                    DEBUG_WARNING("[HotReload] ", nameInProject, " changed on disk while playing : not reloaded");
                    return;
                }
                if(i != 0){
                    DEBUG_WARNING("[HotReload] ", nameInProject, " changed on disk : only the first world is reloaded automatically, reload it by hand");
                    return;
                }
                if(m_Context.worldManager->GetWorldAt(i)->IsDirty()){
                    DEBUG_WARNING("[HotReload] ", nameInProject, " changed on disk but has unsaved changes : not reloaded");
                    return;
                }

                DEBUG_INFO("[HotReload] Reloading world ", nameInProject);
                m_ReloadCurrentWorld = true;
                return;
            }
        }

        void EngineInstance::StartHotReload()
        {
            // Files changing on disk is not something a repeatable run (or a run without a window) can follow
            if(!cvHotReload.Get() || m_EngineSettings.deterministic || m_EngineSettings.headless)
                return;

            Assets::HotReloader::Config config;
            config.files = m_Context.fileManager;
            config.ids = m_Context.assetIDManager;
            config.resources = m_Context.resourcesManager;
            config.remapWorlds = [this](const std::string& oldName, const std::string& newName, bool isDirectory){
                // The project file follows the disk, like the asset database (not in a read-only run)
                m_Context.currentProject->RemapWorlds(oldName, newName, isDirectory, m_EngineSettings.readOnly ? std::string() : m_EngineSettings.project);
            };
            config.onMeshReplaced = [this](const std::string& name){
                Objects::Components::ReloadModelsUsingMesh(*m_Context.worldManager, name);
            };
            config.onWorldChanged = [this](const std::string& name){ ReloadWorldFromDisk(name); };
            if(!m_EngineSettings.readOnly){
                config.persistDatabase = [this]{
                    Serialization::SerializeAssetDataBase(m_Context.currentProject->GetAssetDatabasePath(), *m_Context.assetIDManager);
                };
            }

            m_HotReloader = new Assets::HotReloader();
            if(!m_HotReloader->Start(config)){
                delete m_HotReloader;
                m_HotReloader = nullptr;
            }
        }

        void EngineInstance::PollInput()
        {
            RunMainThreadQueue();

            if(m_HotReloader)
                m_HotReloader->Update();

            SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Input);
            m_Context.platform->GetInput()->Tick();
            m_Context.platform->GetWindow()->PollEvents();
        }

        void EngineInstance::Present()
        {
            // Not sampled here : this thread only hands the frame over. Presentation is the render thread's own timer, taken where it presents
            m_Context.platform->GetWindow()->SwapBuffers();
        }

    }
}