#include "engine.hpp"

#include <iostream>
#include <string>
#include <thread>

#include "engine/core/resources/resources_manager.hpp"
#include "engine/debugging/logger.hpp"
#include "engine/serialization/project/project_serializer.hpp"
#include "engine/levels/level_manager.hpp"
#include "engine/debugging/profiler.hpp"
#include "engine/core/platform/iplatform.hpp"
#include "engine/time/time_manager.hpp"
#include "engine/rendering/camera/camera_manager.hpp"
#include "engine/objects/actors/actor.hpp"
#include "engine/audio/audio_manager.hpp"

using namespace std::chrono;

namespace Shard::Engine{

    namespace Debugging {
        Logger* g_Logger = nullptr;
    }

    namespace Core{
        
        using namespace Levels;
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
            }

            m_Context.fileManager->Init(m_Context.currentProject->GetProjectResourcesPath(), m_Context.fileManager->GetCurrentExecutablePath() / "engine_resources", m_Context.currentProject->GetProjectRoot());
            
            m_Context.resourcesManager->ConstructGlobalFileIndex(m_Context.currentProject->GetProjectResourcesPath());

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
            m_Context.platform->CreateInput();

            m_Context.timeManager->Init(physicsSettings->fixedTimeStep, physicsSettings->maxAccumulatedTime);

            Platform::SystemInfos infos = GetWindow()->GetSystemInfos();

            DEBUG_INFO("===== System infos =====");
            DEBUG_INFO("GPU Vendor : " + infos.gpu_vendor);
            DEBUG_INFO("GPU Renderer : " + infos.gpu_renderer);
            DEBUG_INFO("OpenGL Version : " + infos.gl_version);
            DEBUG_INFO("Using Host : " + std::string(infos.windowHost == Platform::WindowHost::QT ? "QT" : "GLFW") + " with version : " + infos.windowHostVersion);

            DEBUG_INFO("Monitors : ");
            for(int i = 0; i < infos.connectedMonitorsCount; i++){
                DEBUG_INFO("    Monitor : "+ std::to_string(i) + " : width = " + std::to_string(infos.monitors[i].width) + " px, height = "+ std::to_string(infos.monitors[i].height) + " px, refreshRate = "+std::to_string(infos.monitors[i].refreshRate)+" hz");
            }

            if(m_Context.currentProject->GetBuildSettings()->buildIndex.size() > 0){
                Filesystem::Path defaultLevelPath = m_Context.currentProject->GetBuildSettings()->buildIndex[0];
                DEBUG_LOG("Loading default level : "+defaultLevelPath.full);

                // Parallelizes texture/mesh decode across worker threads and keeps pumping window
                // events (+ drawing a splash frame, on platforms that support one) while it waits, so
                // a heavy default level doesn't leave the window looking frozen at boot.
                Platform::IWindow* window = GetWindow();
                GetLevelManager()->LoadLevelBlocking(defaultLevelPath.full, [window](float progress){
                    window->PollEvents();
                    window->DrawLoadingFrame(progress);
                });
            }
            
        }

        void EngineInstance::InitSystems()
        {
            m_Context.renderer = new Rendering::Renderer();
            m_Context.cameraManager = new Rendering::CameraManager();

            m_Context.resourcesManager = new Resources::ResourcesManager();
            m_Context.fileManager = new Filesystem::FileManager();
            m_Context.assetIDManager = new Filesystem::AssetIDManager();

            m_Context.objIDManager = new ObjectIDManager();

            m_Context.levelManager = new Levels::LevelManager();

            m_Context.eventDispatcher = new Events::EventDispatcher();

            m_Context.audioManager = new Audio::AudioManager();
            m_Context.audioIDManager = new Audio::AudioIDManager();

            m_Context.physicsManager = new Physics::PhysicsManager();

            m_Context.timeManager = new Time::TimeManager();

            m_Context.profiler = new Debugging::Profiler();
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

                m_Context.levelManager->GetLevelAt(0)->Serialize(m_Context.levelManager->GetLevelAt(0)->GetPath());

                for(int i = 0; i < m_Context.levelManager->GetLoadedLevelCount(); i++){
                    m_Context.levelManager->GetLevelAt(i)->Play();
                }

                auto* level = m_Context.levelManager->GetLevelAt(0);
                if (level && !level->cameras.empty()){

                    auto cameraEntry = level->cameras.begin();
                    auto camera = cameraEntry->second;
                    if (!camera) {
                        DEBUG_ERROR("First camera is not valid for level : ", level->GetName());
                        return;
                    }

                    auto parent = camera->parent;
                    if (!parent) {
                        DEBUG_ERROR("First camera's parent is not valid for level : ", level->GetName());
                        return;
                    }

                    m_Context.cameraManager->SetActiveCamera(parent->GetID());
                }

                m_ActivateAllPhysics = true;
            }
            else{
                
                for(int i = 0; i < m_Context.levelManager->GetLoadedLevelCount(); i++){
                    m_Context.levelManager->GetLevelAt(i)->Stop();
                }

                m_ReloadCurrentLevel = true;
            }
        }

        bool EngineInstance::ShouldEnd()
        {
            return m_Context.platform && m_Context.platform->GetWindow()->ShouldClose();
        }

        void EngineInstance::Destroy()
        {
            m_Context.currentProject->Shutdown(m_EngineSettings.project);
            m_Context.profiler->Shutdown();
            m_Context.platform->GetInput()->Shutdown();
            m_Context.audioManager->Shutdown();
            m_Context.physicsManager->Shutdown();
            m_Context.levelManager->UnloadAllLevels();
            m_Context.renderer->Shutdown();
            m_Context.platform->GetWindow()->Destroy();
        }

        bool EngineInstance::Run() {

            // Measured first thing in the frame so every system below works off the same delta, and so
            // the time the previous frame actually took is what decides how much simulation is owed.
            m_Context.timeManager->Tick();

            const float fixedDeltaTime = m_Context.timeManager->GetFixedDeltaTime();

            if(m_PlayMode){
                {
                    SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Physics);

                    // Physics runs at a fixed rate, decoupled from the render rate: a frame runs as many
                    // steps as the elapsed real time paid for (0 when rendering outpaces the simulation,
                    // several when it lags behind). Stepping once per frame instead would tie the speed
                    // of the whole simulation to the framerate.
                    const int steps = m_Context.timeManager->ConsumeFixedSteps();

                    for(int i = 0; i < steps; i++){
                        // Intermediate steps need the bodies re-synced in between (kinematic targets
                        // pushed to Jolt, dynamic results read back); the last step's results are
                        // picked up by the TickBodies() call further down, before rendering.
                        if(i > 0)
                            m_Context.physicsManager->TickBodies(fixedDeltaTime);

                        m_Context.physicsManager->StepSimulation(fixedDeltaTime, m_ActivateAllPhysics);

                        // Cleared here rather than once per frame: the first frames of play mode can
                        // run zero steps, and the wake-up has to survive until a step actually happens.
                        m_ActivateAllPhysics = false;
                    }
                }
                {
                    SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Audio);
                    m_Context.audioManager->Tick();
                }
                {
                    SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Scripting);
                    m_Context.levelManager->Tick();
                }
            }
            else{
                // Nothing is simulating, so don't bank time that would be replayed as a burst of steps
                // the moment play mode starts.
                m_Context.timeManager->ResetAccumulator();
            }

            m_Context.levelManager->PumpAsyncLoad();

            if(m_ReloadCurrentLevel)
            {
                std::string levelNameInProject = GetAssetIDManager()->GetAssetFromID(GetLevelManager()->GetLevelAt(0)->GetAssetID())->baseInfos.nameInProject;
                GetLevelManager()->UnloadLevel(0);
                GetResourcesManager()->UnloadLevel(levelNameInProject);
                GetRenderer()->ClearPassesContent();
                GetObjectIDManager()->Reset();
                auto level = GetResourcesManager()->GetLevel(levelNameInProject);
                if(level)
                {
                    GetLevelManager()->LoadLevel(level);
                    GetResourcesManager()->CollectUnused();
                }
                else
                    DEBUG_ERROR("Error re-loading level !");
                m_ReloadCurrentLevel = false;
            }

            {
                SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Physics);
                m_Context.physicsManager->TickBodies(fixedDeltaTime);
            }

            {
                SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Rendering);
                m_Context.renderer->Render();
            }

            {
                SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Input);
                m_Context.platform->GetInput()->Tick();
                m_Context.platform->GetWindow()->PollEvents();
            }

            {
                SHARD_PROFILE_SCOPE(Debugging::ProfileCategory::Presentation);
                m_Context.platform->GetWindow()->SwapBuffers();
            }

            m_Context.profiler->EndFrameSampling();

            if(m_Context.platform->GetInput()->WasKeyPressed(Key::Escape))
            {
                return false;
            }

            return true;
        }

    }
}