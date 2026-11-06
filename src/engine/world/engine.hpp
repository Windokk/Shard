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
            Platform::IPlatform* platform = nullptr;

            std::string project = "";

            std::string startWorld = "";

            std::string scriptFile = "";

            int windowWidth = 1280;
            int windowHeight = 720;
            bool fullscreen = false;
            bool vsync = true;

            glm::vec3 gravity = glm::vec3(0, -9.81f, 0);
            bool overrideGravity = false;

            bool startInPlayMode = false;

            bool readOnly = false;

            bool headless = false;

            // Repeatable run: every frame is one fixed step, single-thread physics, GUIDs counted from the seed, no hot reload. Same seed, project and input end in the same state (HashSimulationState).
            bool deterministic = false;

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
                virtual JobSystem* GetJobSystem() const = 0;
                virtual ThreadPool* GetThreadPool(PoolKind kind) const = 0;
                virtual FrameAllocator* GetFrameAllocator() const = 0;
                virtual Ecs::Registry* GetEcs() const = 0;
                virtual Ecs::Scheduler* GetScheduler() const = 0;
                virtual Assets::HotReloader* GetHotReloader() const { return nullptr; }

                virtual bool IsInPlayMode() const = 0;
                virtual void SetPlayMode(bool on) = 0;

                virtual uint64_t GetFrameNumber() const { return 0; }
                virtual bool IsDeterministic() const { return false; }
                virtual uint64_t GetSimulationSeed() const { return 0; }
                // Named stream: same name and seed give the same sequence whatever the system start order. Keep it, don't ask each frame.
                // Gameplay uses this instead of rand() / std::random_device. Member, not a free function, because the game module has its own copy of the engine's statics.
                virtual Random MakeRandomStream(std::string_view name) const { return Random(Fnv1a64(name)); }
        };

        class EngineInstance : public IEngineContext {
            public:
                EngineInstance(const EngineInstance&) = delete;
                EngineInstance& operator=(const EngineInstance&) = delete;

                static EngineInstance& GetInstance() {
                    static EngineInstance instance;
                    return instance;
                }

                bool ShouldEnd() override;
                void Destroy() override;
                bool Run() override;
                void Init(EngineCreationSettings settings) override;
                void InitSystems() override;

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

                void RegisterEngineSystems();
                void RegisterConsoleCommands();
                void StepPhysics(int step, float fixedDeltaTime);
                void TickAudio();
                enum class ScriptTick { Fixed, Update, Late };
                void TickScripts(ScriptTick kind);
                void UpdateWorlds();
                void SyncPhysicsBodies(float fixedDeltaTime);
                void RenderFrame();
                void PollInput();
                void StartHotReload();
                void ReloadWorldFromDisk(const std::string& nameInProject);
                void Present();

                void StartRenderThread();
                void StopRenderThread();

                // Run at the start of the next frame on the main thread, which owns the world and resources. Safe from any thread.
                void PostToMainThread(std::function<void()> work);
                void RunMainThreadQueue();
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

                uint32_t m_InputEventListener = 0;

                EngineCreationSettings m_EngineSettings;

                bool m_PlayMode = false;
                bool m_ActivateAllPhysics = false;
                bool m_ReloadCurrentWorld = false;
                
                std::shared_ptr<Rendering::RendererSettings> m_RendererSettings;
        };

        #if defined(BUILD_ENGINE)

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
