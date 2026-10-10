#include "engine/world/engine.hpp"

#include <iostream>
#include <string>
#include <thread>

#include "engine/assets/resources_manager.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/core/ecs/registry.hpp"
#include "engine/core/ecs/scheduler.hpp"
#include "engine/core/jobs/job_system.hpp"
#include "engine/core/jobs/thread_pool.hpp"
#include "engine/core/config/cvar.hpp"
#include "engine/core/memory/frame_allocator.hpp"
#include "engine/core/memory/memory_tracker.hpp"
#include "engine/platform/thread/thread.hpp"
#include "engine/assets/serialization/project/project_serializer.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/core/diagnostics/profiler.hpp"
#include "engine/platform/windowing/iplatform.hpp"
#include "engine/platform/hardware/system_info.hpp"
#include "engine/world/time_manager.hpp"
#include "engine/renderer/components/camera_manager.hpp"
#include "engine/world/actor.hpp"
#include "engine/audio/audio_manager.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/audio/audio_world_data.hpp"
#include "engine/physics/physics_world_data.hpp"
#include "engine/renderer/components/render_world_data.hpp"

using namespace std::chrono;

namespace Shard::Engine{

    namespace Debugging {
        Logger* g_Logger = nullptr;
    }

    namespace Core{
        
        namespace {
            // Published by the engine for the console (`list jobs`) : what the job system actually got
            CVar<int> cvJobThreads("jobs.threads", 0, "Threads that run jobs, the main thread included", CVarFlags::ReadOnly);

            // Per-frame scratch memory budget, read when the engine starts (a change applies at the next start)
            CVar<int> cvFrameKB("memory.frame_kb", 4096, "Per-frame scratch memory, in KB, per buffered frame (applies at startup)",
                                CVarFlags::Archive, 64, 1048576);

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

            InitSystems();

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
                LoadConfigLayer(ConfigLayer::Project, ConfigPath(project->GetProjectRoot(), "project.cfg"));
            }

            m_Context.fileManager->Init(m_Context.currentProject->GetProjectResourcesPath(), m_Context.fileManager->GetCurrentExecutablePath() / "engine_resources", m_Context.currentProject->GetProjectRoot());
            
            m_Context.resourcesManager->ConstructGlobalFileIndex(m_Context.currentProject->GetProjectResourcesPath(), m_Context.currentProject->GetAssetDatabasePath());

            m_Context.platform->CreateWindow("Shard", settings.windowWidth, settings.windowHeight, settings.fullscreen, settings.vsync, settings.api);

            Projects::PhysicsSettings* physicsSettings = m_Context.currentProject->GetPhysicsSettings();

            if(settings.overrideGravity)
                physicsSettings->gravity = settings.gravity;

            m_Context.physicsManager->Init(physicsSettings->gravity);
            m_Context.audioManager->Init(100.0f);
            m_RendererSettings = std::make_shared<RendererSettings>();
            m_RendererSettings->viewportWidth = m_Context.platform->GetWindow()->GetFramebufferWidth();
            m_RendererSettings->viewportHeight = m_Context.platform->GetWindow()->GetFramebufferHeight();
            m_RendererSettings->api = (RendererAPI::API)settings.api;
            m_Context.renderer->Init(m_RendererSettings);
            m_Context.renderer->SetOcclusionCullingEnabled(m_Context.currentProject->GetRenderingSettings()->occlusionCulling);
            m_Context.platform->CreateInput();

            m_Context.timeManager->Init(physicsSettings->fixedTimeStep, physicsSettings->maxAccumulatedTime);

            Platform::SystemInfos infos = GetWindow()->GetSystemInfos();

            DEBUG_INFO("===== System infos =====");
            DEBUG_INFO("GPU Vendor : " + infos.gpu_vendor);
            DEBUG_INFO("GPU Renderer : " + infos.gpu_renderer);
            DEBUG_INFO("OpenGL Version : " + infos.gl_version);
            DEBUG_INFO("Using Host : " + std::string(infos.windowHost == Platform::WindowHost::QT ? "QT" : "SDL3") + " with version : " + infos.windowHostVersion);
            DEBUG_INFO("CPU : " + infos.cpuBrand + " (" + std::to_string(infos.cpuLogicalCores) + " threads)");
            DEBUG_INFO("RAM : " + std::to_string(infos.totalRamMB) + " MB");

            DEBUG_INFO("Monitors : ");
            for(int i = 0; i < infos.connectedMonitorsCount; i++){
                DEBUG_INFO("    Monitor : "+ std::to_string(i) + " : width = " + std::to_string(infos.monitors[i].width) + " px, height = "+ std::to_string(infos.monitors[i].height) + " px, refreshRate = "+std::to_string(infos.monitors[i].refreshRate)+" hz");
            }

            std::string startWorld = settings.startWorld;
            if(startWorld.empty() && m_Context.currentProject->GetBuildSettings()->buildIndex.size() > 0)
                startWorld = m_Context.currentProject->GetBuildSettings()->buildIndex[0].full;

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
            }
            
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

                stats.actors = m_Context.worldManager->GetWorldAt(0)->transforms.size();
            });

            m_Context.profiler->AddStatsProvider([](Debugging::MinimalStatistics& stats)
            {
                stats.gpuMemoryMB = Platform::ProcessGpuMemoryUsage() / (1024.0f * 1024.0f);
            });

            RegisterEngineSystems();
        }

        void EngineInstance::RegisterEngineSystems()
        {
            using namespace Ecs;
            Scheduler& scheduler = *m_Context.scheduler;

            // What the engine's managers do each frame, expressed as systems of the phases. They touch the managers (not
            // components), which the read / write declaration can't express, hence Exclusive : they run alone, in this order.
            auto add = [&scheduler](const char* name, Phase phase, SystemFn fn)
            {
                SystemDesc desc;
                desc.name = name;
                desc.phase = phase;
                desc.exclusive = true;
                desc.run = std::move(fn);
                scheduler.Add(std::move(desc));
            };

            add("Input.Poll", Phase::Input, [this](SystemContext&){ PollInput(); });

            add("Worlds.Update", Phase::PreSim, [this](SystemContext&){ UpdateWorlds(); });

            // Physics runs at a fixed rate, decoupled from the render rate : a frame runs as many steps as the elapsed real
            // time paid for (0 when rendering outpaces the simulation, several when it lags behind).
            add("Physics.Step", Phase::Fixed, [this](SystemContext& c){ if(m_PlayMode) StepPhysics(c.fixedStep, c.fixedDeltaTime); });

            add("Audio.Tick", Phase::Update, [this](SystemContext&){ if(m_PlayMode) TickAudio(); });
            add("Scripts.Tick", Phase::Update, [this](SystemContext&){ if(m_PlayMode) TickScripts(); });

            // After the scripts moved things : kinematic targets go to Jolt, dynamic results come back to the transforms
            add("Physics.Sync", Phase::Late, [this](SystemContext& c){ SyncPhysicsBodies(c.fixedDeltaTime); });

            add("Render.Frame", Phase::Extract, [this](SystemContext&){ RenderFrame(); });
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
            m_PlayMode = on;

            if(m_PlayMode){

                if(!m_EngineSettings.readOnly)
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
            }
            else{
                
                for(int i = 0; i < m_Context.worldManager->GetLoadedWorldCount(); i++){
                    m_Context.worldManager->GetWorldAt(i)->Stop();
                }

                m_ReloadCurrentWorld = true;
            }
        }

        bool EngineInstance::ShouldEnd()
        {
            return m_Context.platform && m_Context.platform->GetWindow()->ShouldClose();
        }

        void EngineInstance::Destroy()
        {
            if(!m_EngineSettings.readOnly)
                m_Context.currentProject->Shutdown(m_EngineSettings.project, *m_Context.assetIDManager);
            m_Context.profiler->Shutdown();
            Debugging::Profiler::SetActive(nullptr);
            m_Context.platform->GetInput()->Shutdown();
            m_Context.audioManager->Shutdown();
            m_Context.physicsManager->Shutdown();
            m_Context.worldManager->UnloadAllWorlds();
            m_Context.renderer->Shutdown();
            m_Context.platform->GetWindow()->Destroy();

            // Last : every system that submitted work is shut down, and every JobGroup has been waited on. The pools'
            // queued tasks are run to the end first (they may use the job system), then the job system goes.
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

            // Measured first thing in the frame so every system below works off the same delta, and so
            // the time the previous frame actually took is what decides how much simulation is owed.
            m_Context.timeManager->Tick();

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

            m_Context.profiler->EndFrameSampling(m_Context.timeManager->GetDeltaTime() * 1000.0f);

            if(m_Context.platform->GetInput()->WasKeyPressed(Key::Escape))
            {
                return false;
            }

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

        void EngineInstance::TickScripts()
        {
            SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Scripting);
            m_Context.worldManager->Tick();
        }

        void EngineInstance::UpdateWorlds()
        {
            m_Context.worldManager->PumpAsyncLoad();

            if(m_ReloadCurrentWorld)
            {
                std::string worldNameInProject = GetAssetIDManager()->GetAssetFromID(GetWorldManager()->GetWorldAt(0)->GetAssetID())->baseInfos.nameInProject;
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
            SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Rendering);
            m_Context.renderer->Render();
        }

        void EngineInstance::PollInput()
        {
            SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Input);
            m_Context.platform->GetInput()->Tick();
            m_Context.platform->GetWindow()->PollEvents();
        }

        void EngineInstance::Present()
        {
            SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Presentation);
            m_Context.platform->GetWindow()->SwapBuffers();
        }

    }
}