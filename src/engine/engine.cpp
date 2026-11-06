#include "engine/c_abi_render.hpp"
#include "engine/world/engine.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/assets/asset_importer.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <limits>
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
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
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
            CVar<int> cvJobThreads("jobs.threads", 0, "Threads that run jobs, the main thread included", CVarFlags::ReadOnly);

            CVar<float> cvFrameDt("time.frame_dt", 0.0f, "Length in seconds of every frame, 0 : real time (deterministic runs)", CVarFlags::None, 0.0f, 1.0f);

            CVar<bool> cvRenderThread("render.thread", true, "Draw on a dedicated render thread (applies at startup)", CVarFlags::Archive);
            CVar<bool> cvVsync("render.vsync", true, "Vertical sync : wait for the display before showing a frame", CVarFlags::Archive);
            CVar<int> cvEngineMaxFPS("engine.maxFPS", 0, "Global frame rate limit, main thread included, 0 : unlimited", CVarFlags::Archive, 0, 10000);
            CVar<int> cvMaxFPS("render.maxFPS", 0, "Frame rate limit of the render thread alone, 0 : unlimited (the vsync, when on, limits it to the display's rate anyway)", CVarFlags::Archive, 0, 10000);

            // sleep_until rounds up to the Windows scheduler tick (15.6 ms), so any limit under ~400 fps came out as 64 fps. PreciseSleep uses a high resolution timer and spins at the end.
            void HoldUntil(std::chrono::steady_clock::time_point frameStart, int fps)
            {
                SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Waiting);
                const auto target = frameStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / fps));
                const double remaining = std::chrono::duration<double>(target - std::chrono::steady_clock::now()).count();
                Core::Platform::PreciseSleep(remaining);
            }
            CVar<bool> cvRenderDoc("render.renderdoc", false, "Load RenderDoc before the graphics API starts, so that `render.capture` can take a capture (applies at startup, SHARD_RENDERDOC=1 does the same)");
            CVar<bool> cvVulkanValidation("render.vulkan_validation", false, "Vulkan : turn the validation layer on (needs the Vulkan SDK ; applies at startup, SHARD_VULKAN_VALIDATION=1 does the same)");

            CVar<bool> cvHotReload("assets.hot_reload", true, "Reload the assets changed on disk while the engine runs (applies at startup)", CVarFlags::Archive);

            CVar<int> cvFramesInFlight("render.frames_in_flight", 2, "Frames the simulation may run ahead of the render thread",
                                       CVarFlags::Archive, 1, 4);

            CVar<float> cvRebaseDistance("render.rebase_distance", 4096.0f, "Large worlds : meters the camera may get from the render origin before the origin moves to it (0 : always at the camera)", CVarFlags::Archive, 0.0f, 1.0e9f);
            CVar<bool> cvPhysicsInterpolate("physics.interpolate", true, "Draw physics bodies between their last two steps (smooth motion at any frame rate)", CVarFlags::Archive);

            CVar<int> cvFrameKB("memory.frame_kb", 4096, "Per-frame scratch memory, in KB, per buffered frame (applies at startup)",
                                CVarFlags::Archive, 64, 1048576);

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

            {
                const uint64_t seed = settings.seed ? *settings.seed : Determinism::Seed();
                Determinism::Configure(seed, settings.deterministic);
                DEBUG_INFO(std::string(settings.deterministic ? "Deterministic run" : "Run") + ", seed " + std::to_string(seed) +
                           (settings.seed ? "" : " (drawn : --seed " + std::to_string(seed) + " repeats it)"));
            }

            InitSystems();

            Plugins::PluginManager::Get().OnEngineSystemsReady(this, m_Context.scheduler, m_Context.eventDispatcher);

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
                Projects::ApplyTagSettings(*project->GetTagSettings());
                LoadConfigLayer(ConfigLayer::Project, ConfigPath(project->GetProjectRoot(), "project.cfg"));
            }

            m_Context.fileManager->Init(m_Context.currentProject->GetProjectResourcesPath(), m_Context.fileManager->GetCurrentExecutablePath() / "engine_resources", m_Context.currentProject->GetProjectRoot());
            
            Rendering::TextureCache::SetDirectory((std::filesystem::path(m_Context.currentProject->GetProjectRoot().full) / ".cache" / "textures").generic_string());
            Rendering::SetLightmapUnwrapCacheDirectory((std::filesystem::path(m_Context.currentProject->GetProjectRoot().full) / ".cache" / "lightmap_uv").generic_string());

            m_Context.resourcesManager->ConstructGlobalFileIndex(m_Context.currentProject->GetProjectResourcesPath(), m_Context.currentProject->GetAssetDatabasePath());

            // RenderDoc must be in the process before the graphics API starts.
            {
                const char* renderDocEnvironment = std::getenv("SHARD_RENDERDOC");
                const bool renderDocWanted = renderDocEnvironment != nullptr || cvRenderDoc.Get();
                if(Rendering::RenderDoc::Get().Attach(renderDocWanted, (renderDocEnvironment && std::string(renderDocEnvironment) != "1") ? renderDocEnvironment : ""))
                    Rendering::RenderDoc::Get().SetCapturePrefix((std::filesystem::path(m_Context.currentProject->GetProjectRoot().full) / ".cache" / "captures" / "shard").generic_string());
            }

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
            // Probe manager lives on the render thread. World and resource data go through the main thread's queue.
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

            // Registered after the input's own listener so input state is current when handlers run.
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

            {
                Plugins::PluginManager& plugins = Plugins::PluginManager::Get();
                Plugins::PluginManager::Callbacks callbacks;
                callbacks.hasLoadedWorlds = [this]{ return m_Context.worldManager->GetLoadedWorldCount() > 0; };
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

                Platform::IWindow* window = GetWindow();
                GetWorldManager()->LoadWorldBlocking(startWorld, [window](float progress){
                    window->PollEvents();
                    window->DrawLoadingFrame(progress);
                });
                Plugins::PluginManager::Get().AfterWorldLoad(startWorld);
            }

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
            Rendering::RegisterRenderingModule();
            Physics::RegisterPhysicsModule();
            Audio::RegisterAudioModule();

            JobSystemDesc jobDesc;
            jobDesc.workerCount = Platform::WorkerThreadCount();
            if(const auto pinned = Platform::GetEnv("SHARD_JOB_THREADS")){
                try { jobDesc.workerCount = static_cast<unsigned>(std::max(1, std::stoi(*pinned))); } catch(...) {}
            }
            jobDesc.onWorkerStart = [](unsigned index){ Platform::SetCurrentThreadName("Shard Job " + std::to_string(index)); };
            m_Context.jobSystem = new JobSystem(jobDesc);
            m_Context.threadPools = new ThreadPools([](PoolKind kind, const std::string& name, unsigned index){
                Platform::SetCurrentThreadName("Shard " + name + " " + std::to_string(index));
                if(kind == PoolKind::Audio)
                    Platform::SetCurrentThreadPriority(Platform::ThreadPriority::High);
            });
            DEBUG_INFO("Job system : " + std::to_string(m_Context.jobSystem->ThreadCount()) + " threads");

            m_Context.frameAllocator = new FrameAllocator(size_t(cvFrameKB.Get()) * 1024, 2, "FrameAllocator");
            cvJobThreads.ForceSet(static_cast<int>(m_Context.jobSystem->ThreadCount()));

            m_Context.renderer = new Rendering::Renderer();
            m_Context.cameraManager = new Rendering::CameraManager();

            m_Context.renderer->onViewportResized = [this](int width, int height) { m_Context.cameraManager->UpdateSize(width, height); };

            m_Context.assetIDManager = new Filesystem::AssetIDManager();
            m_Context.fileManager = new Filesystem::FileManager(*m_Context.assetIDManager);
            m_Context.resourcesManager = new Resources::ResourcesManager(*m_Context.fileManager, *m_Context.assetIDManager);

            LoadConfigLayer(ConfigLayer::Engine, ConfigPath(m_Context.fileManager->GetCurrentExecutablePath(), "engine.cfg"));
            LoadConfigLayer(ConfigLayer::User, ConfigPath(m_Context.fileManager->GetCurrentExecutablePath(), "user.cfg"));

            m_Context.objIDManager = new ObjectIDManager();

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

            // Exclusive systems: they touch managers, which read/write declarations cannot express, so they run alone in this order.
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

            add("Scripts.FixedTick", Phase::Fixed, [this](SystemContext&){ if(m_PlayMode) TickScripts(ScriptTick::Fixed); });
            add("Physics.Step", Phase::Fixed, [this](SystemContext& c){ if(m_PlayMode) StepPhysics(c.fixedStep, c.fixedDeltaTime); });

            add("Audio.Tick", Phase::Update, [this](SystemContext&){ if(m_PlayMode) TickAudio(); });
            add("Scripts.Tick", Phase::Update, [this](SystemContext&){
                if(m_PlayMode) TickScripts(ScriptTick::Update);
                else m_Context.worldManager->EditTick();
            });

            add("ReflectionProbes.Tick", Phase::Update, [](SystemContext&){ Objects::Components::ReflectionProbe::TickAll(); }, true);

            add("Physics.Sync", Phase::Late, [this](SystemContext& c){ SyncPhysicsBodies(c.fixedDeltaTime); });
            add("Scripts.LateTick", Phase::Late, [this](SystemContext&){ if(m_PlayMode) TickScripts(ScriptTick::Late); });

            auto extractor = std::make_shared<Rendering::RenderExtractor>();
            auto poseOffsets = std::make_shared<Rendering::RenderExtractor::PoseOffsets>();
            extractor->SetPoseOffsets(poseOffsets.get());
            extractor->AddSnapshotProvider(&Plugins::CAbi::ExtractPluginRender);
            add("Render.Extract", Phase::Extract, [this, extractor, poseOffsets](SystemContext& c){
                extractor->SetRebaseDistance(double(cvRebaseDistance.Get()));
                {
                    SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Waiting);
                    Rendering::RenderThread::Get().WaitForFrameSlot();
                }

                SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Rendering);

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

            CVarRegistry::Global().RegisterCommand("render.camera", "render.camera [off | <px> <py> <pz> <tx> <ty> <tz> [fov]] : the view the frames (and raytrace.render) are seen from in place of the active camera's : a position, the point it looks at, the vertical field of view ; the simulation is left alone. No argument : the view now, and how far the meshes reach",
                [this, extractor](const std::vector<std::string>& args) {
                    const char* usage = "usage : render.camera [off | <px> <py> <pz> <tx> <ty> <tz> [fov]]";
                    if(args.size() == 1 && args[0] == "off")
                        extractor->SetCameraOverride(std::nullopt);
                    else if(args.size() == 6 || args.size() == 7){
                        float v[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                        try {
                            for(size_t i = 0; i < args.size(); i++)
                                v[i] = std::stof(args[i]);
                        } catch(...) {
                            return CommandResult{false, usage};
                        }
                        Rendering::RenderExtractor::CameraOverride view;
                        view.position = glm::vec3(v[0], v[1], v[2]);
                        view.target = glm::vec3(v[3], v[4], v[5]);
                        view.fovDegrees = v[6];
                        if(glm::length(view.target - view.position) < 1e-4f)
                            return CommandResult{false, "the camera cannot look at its own position"};
                        extractor->SetCameraOverride(view);
                    }
                    else if(!args.empty())
                        return CommandResult{false, usage};

                    const Rendering::RenderSnapshot& snapshot = m_Context.renderer->GetSnapshot();
                    auto vec = [](const glm::vec3& p) {
                        char text[96];
                        std::snprintf(text, sizeof(text), "(%.3f %.3f %.3f)", p.x, p.y, p.z);
                        return std::string(text);
                    };
                    glm::vec3 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
                    for(const Rendering::MeshProxy& mesh : snapshot.meshes){
                        if(!mesh.mesh || mesh.mesh->GetBoundsMin().x > mesh.mesh->GetBoundsMax().x)
                            continue;
                        for(int corner = 0; corner < 8; corner++){
                            const glm::vec3 a = mesh.mesh->GetBoundsMin(), b = mesh.mesh->GetBoundsMax();
                            const glm::vec3 local((corner & 1) ? b.x : a.x, (corner & 2) ? b.y : a.y, (corner & 4) ? b.z : a.z);
                            const glm::vec3 p = glm::vec3(mesh.worldMatrix * glm::vec4(local, 1.0f));
                            low = glm::min(low, p);
                            high = glm::max(high, p);
                        }
                    }
                    std::string text = std::string(extractor->GetCameraOverride() ? "override" : "active camera") + (args.empty() ? "" : " (from the next frame)");
                    if(snapshot.camera.valid)
                        text += " : at " + vec(snapshot.camera.position) + " looking along " + vec(snapshot.camera.forward) + ", fov " + std::to_string(snapshot.camera.fovDegrees);
                    if(low.x <= high.x)
                        text += "\nmeshes from " + vec(low) + " to " + vec(high);
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

            CVarRegistry::Global().RegisterCommand("sim.hash", "sim.hash : the hash of the simulation state (transforms, physics bodies, scripts' fields), and the seed of the run",
                [this](const std::vector<std::string>&) {
                    return CommandResult{true, HashSimulationState(*this).Describe() + ", seed " + std::to_string(GetSimulationSeed()) +
                                               (m_EngineSettings.deterministic ? ", deterministic" : ", not deterministic")};
                });

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

            // The graphics context must be back on this thread before teardown.
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
            // Plugins stop before the systems and renderer they registered into are destroyed.
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
            CVarRegistry::Global().UnregisterCommand("render.camera");
            CVarRegistry::Global().UnregisterCommand("asset.import");
            CVarRegistry::Global().UnregisterCommand("render.features");

            // Run queued pool tasks to the end first (they may use the job system), then shut the job system down.
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

            if(!m_EngineSettings.readOnly && m_Context.fileManager)
            {
                const std::string userConfig = ConfigPath(m_Context.fileManager->GetCurrentExecutablePath(), "user.cfg");
                CVarRegistry& registry = CVarRegistry::Global();
                const std::string archive = registry.ArchiveText();
                const bool hasSomethingToSave = archive.find('\n') + 1 < archive.size();
                if(hasSomethingToSave || Filesystem::Path(userConfig).Exists())
                    registry.SaveArchive(userConfig);
            }

            const std::string leaks = MemoryTracker::Get().Report();
            if(!leaks.empty())
                DEBUG_WARNING("Memory not released at shutdown : " + leaks);
        }

        bool EngineInstance::Run() {

            const auto frameStart = std::chrono::steady_clock::now();

            RunScriptedCommands();
            m_FrameCount++;
            Plugins::PluginManager::Get().BeginFrame(m_FrameCount);

            // Sampled first so every system sees the same delta. Deterministic runs skip the clock and use one fixed step.
            m_Context.timeManager->Tick(m_EngineSettings.deterministic ? m_Context.timeManager->GetFixedDeltaTime() : cvFrameDt.Get());

            m_Context.frameAllocator->NextFrame();

            Ecs::Scheduler::FrameTiming timing;
            timing.deltaTime = m_Context.timeManager->GetDeltaTime();
            timing.fixedDeltaTime = m_Context.timeManager->GetFixedDeltaTime();
            if(m_PlayMode)
                timing.fixedSteps = m_Context.timeManager->ConsumeFixedSteps();
            else
                // Drop banked time, or play mode starts with a burst of replayed steps.
                m_Context.timeManager->ResetAccumulator();

            // Input -> PreSim -> Fixed x N -> PostPhysics -> Update -> Late -> Extract
            m_Context.scheduler->RunFrame(*m_Context.ecs, m_Context.jobSystem, timing);

            Present();

            if(const int maxFPS = cvEngineMaxFPS.Get(); maxFPS > 0)
                HoldUntil(frameStart, maxFPS);

            m_Context.profiler->EndFrameSampling(std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - frameStart).count(), m_FrameCount);

            Plugins::PluginManager::Get().EndFrame(m_FrameCount);

            return true;
        }

        void EngineInstance::StepPhysics(int step, float fixedDeltaTime)
        {
            SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Physics);

            // Intermediate steps re-sync bodies. The last step's results are picked up by Late's Physics.Sync.
            if(step > 0)
                m_Context.physicsManager->TickBodies(fixedDeltaTime);

            m_Context.physicsManager->CollectGravityFields();
            m_Context.physicsManager->StepSimulation(fixedDeltaTime, m_ActivateAllPhysics);

            // Cleared here, not per frame: play mode can run zero steps at first and the wake-up must survive until a step runs.
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
            Rendering::Renderer* renderer = m_Context.renderer;
            Debugging::Profiler* profiler = m_Context.profiler;
            const uint64_t frame = m_FrameCount;
            if(Platform::IWindow* window = GetWindow())
                window->PrepareFrame();
            Rendering::RenderThread::Get().SubmitFrame([renderer, profiler, frame] {
                static std::chrono::steady_clock::time_point lastStart = std::chrono::steady_clock::now();
                if(const int maxFPS = cvMaxFPS.Get(); maxFPS > 0)
                    HoldUntil(lastStart, maxFPS);
                lastStart = std::chrono::steady_clock::now();

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
            if(!cvHotReload.Get() || m_EngineSettings.deterministic || m_EngineSettings.headless)
                return;

            Assets::HotReloader::Config config;
            config.files = m_Context.fileManager;
            config.ids = m_Context.assetIDManager;
            config.resources = m_Context.resourcesManager;
            config.remapWorlds = [this](const std::string& oldName, const std::string& newName, bool isDirectory){
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
            m_Context.platform->GetWindow()->SwapBuffers();
        }

    }
}