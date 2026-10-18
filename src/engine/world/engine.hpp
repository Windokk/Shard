#pragma once

#include <cstdint>
#include <stdexcept>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <glm/glm.hpp>

#include "engine/core/math/random.hpp"

namespace Shard::Engine::Assets { class HotReloader; }

namespace Shard::Engine{

    namespace Rendering {
        class Renderer;
        class IRenderContext;
        class CameraManager;
        struct RendererSettings;
    }

    namespace Events {
        class EventDispatcher;
    }

    namespace Worlds{
        class WorldManager;
    }

    namespace Audio{
        class AudioManager;
        class AudioIDManager;
    }

    namespace Physics{
        class PhysicsManager;
    }

    namespace Time{
        class TimeManager;
    }

    namespace Projects{
        class Project;
    }

    namespace Debugging{
        class Profiler;
    }

    namespace Projects{
        struct BuildSettings;
    }

    namespace Filesystem{
        class FileManager;
        class AssetIDManager;
    }

    namespace Core {

        namespace Resources{
            class ResourcesManager;
        }

        namespace Platform{
            class IPlatform;
            class IInput;
            class IWindow;
        }

        class ObjectIDManager;
        class JobSystem;
        class ThreadPool;
        class FrameAllocator;
        class ThreadPools;
        namespace Ecs {
            class Registry;
            class Scheduler;
        }
        enum class PoolKind : uint8_t;

        struct EngineCreationSettings{
            //PLATFORM
            Platform::IPlatform* platform = nullptr;

            //PROJECT
            std::string project = "";

            /// Command line override for the world loaded at startup (a path in the project, such as "cornell.world");
            /// the first world of the project's build settings is loaded unless it is set.
            std::string startWorld = "";

            /// A file of console commands to run while the engine runs, one per line : "<frame> <command>" (blank lines and
            /// lines starting with # are ignored). For scripted runs : tests, benchmarks.
            std::string scriptFile = "";

            //WINDOW (Editor preferences)
            int windowWidth = 1280;
            int windowHeight = 720;
            bool fullscreen = false;
            bool vsync = true;

            //PHYSICS (project settings : physics)
            /// Command line override for the project's gravity; the project setting is used unless
            /// overrideGravity is set.
            glm::vec3 gravity = glm::vec3(0, -9.81f, 0);
            bool overrideGravity = false;

            //PLAY MODE
            bool startInPlayMode = false;

            /// Nothing is written back to the project : neither the world saved when play mode starts, nor the
            /// project file and the asset database saved at shutdown. For runs that must leave the project as they
            /// found it (the CI's smoke test).
            bool readOnly = false;

            /// No window system, no GPU, no sound card (a dedicated server, the tests, a replay checked by the CI) : the platform
            /// is a headless one, the audio device is not opened, nothing is watched on disk. The renderer is then to be run on
            /// the null graphics backend (api = RendererAPI::API::Null) : the frame runs, nothing is drawn.
            bool headless = false;

            /// A run that can be repeated : every frame lasts exactly one fixed step (the clock is ignored), physics runs on
            /// one thread, GUIDs are counted from the seed, nothing is reloaded behind the simulation's back. With the same seed,
            /// the same project and the same input it ends in the same state (see HashSimulationState), on every platform.
            bool deterministic = false;

            /// The seed every random stream of the engine derives from (see Core::Determinism). A seed drawn from the OS, and
            /// logged, when it is not set.
            std::optional<uint64_t> seed;

            uint32_t api = 0;
        };

        struct EngineContext {

            Rendering::Renderer* renderer = nullptr;
            Rendering::CameraManager* cameraManager = nullptr;

            Resources::ResourcesManager* resourcesManager = nullptr;
            Filesystem::FileManager* fileManager = nullptr;
            Filesystem::AssetIDManager* assetIDManager = nullptr;

            ObjectIDManager* objIDManager = nullptr;

            Worlds::WorldManager* worldManager = nullptr;

            Events::EventDispatcher* eventDispatcher = nullptr;

            Audio::AudioManager* audioManager = nullptr;

            Audio::AudioIDManager* audioIDManager = nullptr;

            Physics::PhysicsManager* physicsManager = nullptr;

            Time::TimeManager* timeManager = nullptr;

            Platform::IPlatform* platform = nullptr;

            std::shared_ptr<Projects::Project> currentProject = nullptr;

            Debugging::Profiler* profiler = nullptr;

            JobSystem* jobSystem = nullptr;
            ThreadPools* threadPools = nullptr;
            FrameAllocator* frameAllocator = nullptr;

            Ecs::Registry* ecs = nullptr;
            Ecs::Scheduler* scheduler = nullptr;
        };

        class IEngineContext {
            public:
                virtual ~IEngineContext() = default;

                virtual bool ShouldEnd() = 0;
                virtual void Destroy() = 0;
                virtual bool Run() = 0;
                virtual void Init(EngineCreationSettings settings) = 0;
                virtual void InitSystems() = 0;

                virtual void SetSettings(const EngineCreationSettings& s) = 0;
                virtual EngineCreationSettings GetSettings() const = 0;

                virtual Platform::IWindow* GetWindow() const = 0;
                virtual Platform::IInput* GetInputManager() const = 0;

                virtual Rendering::Renderer* GetRenderer() const = 0;
                /// What the parts of the renderer that draw the world (components, features) use of the renderer
                virtual Rendering::IRenderContext* GetRenderContext() const = 0;
                virtual Rendering::CameraManager* GetCameraManager() const = 0;
                virtual Resources::ResourcesManager* GetResourcesManager() const = 0;
                virtual Filesystem::FileManager* GetFileManager() const = 0;
                virtual Filesystem::AssetIDManager* GetAssetIDManager() const = 0;
                virtual ObjectIDManager* GetObjectIDManager() const = 0;
                virtual Physics::PhysicsManager* GetPhysicsManager() const = 0;
                virtual Worlds::WorldManager* GetWorldManager() const = 0;
                virtual Events::EventDispatcher* GetEventDispatcher() const = 0;
                virtual Audio::AudioManager* GetAudioManager() const = 0;
                virtual Audio::AudioIDManager* GetAudioIDManager() const = 0;
                virtual Time::TimeManager* GetTimeManager() const = 0;
                virtual Projects::BuildSettings* GetBuildSettings() const = 0;
                virtual std::shared_ptr<Projects::Project> GetCurrentProject() const = 0;
                virtual Debugging::Profiler* GetProfiler() const = 0;
                /// The engine's worker pool (work-stealing jobs, task graphs, ParallelFor) : see engine/core/jobs/
                virtual JobSystem* GetJobSystem() const = 0;
                /// A dedicated pool (IO : blocking work, Render / Audio : one thread, tasks in order), created on first
                /// use. Null when the context has none.
                virtual ThreadPool* GetThreadPool(PoolKind kind) const = 0;
                /// Per-frame scratch memory any thread can allocate from ; valid for the current and the previous frame.
                virtual FrameAllocator* GetFrameAllocator() const = 0;
                /// The entities of every loaded world (an actor owns one) : see engine/core/ecs/
                virtual Ecs::Registry* GetEcs() const = 0;
                /// The systems run each frame, by phase (Input, PreSim, Fixed, PostPhysics, Update, Late, Extract)
                virtual Ecs::Scheduler* GetScheduler() const = 0;
                /// Keeps the loaded assets in line with their files on disk (null if it isn't running)
                virtual Assets::HotReloader* GetHotReloader() const { return nullptr; }

                virtual bool IsInPlayMode() const = 0;
                virtual void SetPlayMode(bool on) = 0;

                /// Frames run so far (the calls of Run)
                virtual uint64_t GetFrameNumber() const { return 0; }
                /// Whether this run can be repeated (EngineCreationSettings::deterministic)
                virtual bool IsDeterministic() const { return false; }
                /// The seed the random streams derive from
                virtual uint64_t GetSimulationSeed() const { return 0; }
                /// A random generator for a system of its own : the same name gives the same sequence in every run with the same
                /// seed, whatever order the systems start in. Keep it, do not ask every frame. Gameplay uses this instead of
                /// rand() / std::random_device, which no replay can reproduce. (Here, not a free function : the game module has
                /// its own copy of the engine's statics.)
                virtual Random MakeRandomStream(std::string_view name) const { return Random(Fnv1a64(name)); }
        };

        class EngineInstance : public IEngineContext {
            public:
                EngineInstance(const EngineInstance&) = delete;
                EngineInstance& operator=(const EngineInstance&) = delete;

                // Provide access to the singleton instance
                static EngineInstance& GetInstance() {
                    static EngineInstance instance;
                    return instance;
                }

                // Public interface
                bool ShouldEnd() override;
                void Destroy() override;
                bool Run() override;
                void Init(EngineCreationSettings settings) override;
                void InitSystems() override;

                // Optional : configure engine before initialization
                void SetSettings(const EngineCreationSettings& s) override { m_EngineSettings = s; }
                EngineCreationSettings GetSettings() const override { return m_EngineSettings; }

                Platform::IWindow* GetWindow() const override;

                Platform::IInput* GetInputManager() const override;

                Rendering::Renderer* GetRenderer() const override { return m_Context.renderer; }

                Rendering::IRenderContext* GetRenderContext() const override;

                Rendering::CameraManager* GetCameraManager() const override { return m_Context.cameraManager; }

                Resources::ResourcesManager* GetResourcesManager() const override { return m_Context.resourcesManager; }

                Filesystem::FileManager* GetFileManager() const override { return m_Context.fileManager; }

                Filesystem::AssetIDManager* GetAssetIDManager() const override { return m_Context.assetIDManager; }

                ObjectIDManager* GetObjectIDManager() const override { return m_Context.objIDManager; }

                Physics::PhysicsManager* GetPhysicsManager() const override { return m_Context.physicsManager; }

                Worlds::WorldManager* GetWorldManager() const override { return m_Context.worldManager; }

                Events::EventDispatcher* GetEventDispatcher() const override { return m_Context.eventDispatcher; }

                Audio::AudioManager* GetAudioManager() const override { return m_Context.audioManager; }

                Audio::AudioIDManager* GetAudioIDManager() const override { return m_Context.audioIDManager; }

                Time::TimeManager* GetTimeManager() const override { return m_Context.timeManager; }

                Projects::BuildSettings* GetBuildSettings() const override;

                std::shared_ptr<Projects::Project> GetCurrentProject() const override { return m_Context.currentProject; }

                Debugging::Profiler* GetProfiler() const override { return m_Context.profiler; }

                JobSystem* GetJobSystem() const override { return m_Context.jobSystem; }

                ThreadPool* GetThreadPool(PoolKind kind) const override;

                FrameAllocator* GetFrameAllocator() const override { return m_Context.frameAllocator; }

                Ecs::Registry* GetEcs() const override { return m_Context.ecs; }

                Ecs::Scheduler* GetScheduler() const override { return m_Context.scheduler; }

                Assets::HotReloader* GetHotReloader() const override { return m_HotReloader; }

                bool IsInPlayMode() const override { return m_PlayMode; }

                void SetPlayMode(bool on) override;

                uint64_t GetFrameNumber() const override { return m_FrameCount; }
                bool IsDeterministic() const override { return m_EngineSettings.deterministic; }
                uint64_t GetSimulationSeed() const override;
                Random MakeRandomStream(std::string_view name) const override;

            private:
                EngineInstance() = default;

                // The engine's own systems : what each phase of the frame does (see RegisterEngineSystems)
                void RegisterEngineSystems();
                void RegisterConsoleCommands();
                void StepPhysics(int step, float fixedDeltaTime);
                void TickAudio();
                void TickScripts();
                void UpdateWorlds();
                void SyncPhysicsBodies(float fixedDeltaTime);
                void RenderFrame();
                void PollInput();
                void StartHotReload();
                void ReloadWorldFromDisk(const std::string& nameInProject);
                void Present();

                // The render thread owns the graphics context while it runs (see Rendering::RenderThread)
                void StartRenderThread();
                void StopRenderThread();

                // Work another thread (the render thread, mostly) leaves for the main thread, which owns the world and the
                // resources : it runs at the start of the next frame. Safe to call from any thread.
                void PostToMainThread(std::function<void()> work);
                void RunMainThreadQueue();
                // Console commands to run at given frames (see EngineCreationSettings::scriptFile)
                struct ScriptedCommand { uint64_t frame; std::string line; };
                std::vector<ScriptedCommand> m_Script;
                size_t m_ScriptNext = 0;
                uint64_t m_FrameCount = 0;
                void LoadScript(const std::string& path);
                void RunScriptedCommands();

                std::mutex m_MainQueueMutex;
                std::vector<std::function<void()>> m_MainQueue;

                EngineContext m_Context;

                Assets::HotReloader* m_HotReloader = nullptr;

                // Turns the OS input events into engine events (Events::TranslateInputEvent), 0 : not listening
                uint32_t m_InputEventListener = 0;

                EngineCreationSettings m_EngineSettings;

                bool m_PlayMode = false;
                bool m_ActivateAllPhysics = false;
                bool m_ReloadCurrentWorld = false;
                
                std::shared_ptr<Rendering::RendererSettings> m_RendererSettings;
        };

        #if defined(BUILD_ENGINE)

            // Used by the EXE/engine
            inline IEngineContext* gSharedEnginePtr = nullptr;

            inline void SetEngine(IEngineContext* ptr) {
                gSharedEnginePtr = ptr;
            }

            inline IEngineContext& GetEngine() {
                if (!gSharedEnginePtr)
                    exit(2);
                return *gSharedEnginePtr;
            }

        #elif defined(BUILD_GAME)

            // Used by the Game Module DLL
            inline IEngineContext* gSharedEnginePtr = nullptr;

            inline void SetEngine(IEngineContext* ptr) {
                gSharedEnginePtr = ptr;
            }

            inline IEngineContext& GetEngine() {
                if (!gSharedEnginePtr)
                    exit(2);
                return *gSharedEnginePtr;
            }
        #endif
    }
   
}
