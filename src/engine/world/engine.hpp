#pragma once

#include <stdexcept>
#include <memory>
#include <glm/glm.hpp>

namespace Shard::Engine{

    namespace Rendering {
        class Renderer;
        class CameraManager;
        struct RendererSettings;
    }

    namespace Events {
        class EventDispatcher;
    }

    namespace Levels{
        class LevelManager;
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

        struct EngineCreationSettings{
            //PLATFORM
            Platform::IPlatform* platform = nullptr;

            //PROJECT
            std::string project = "";

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

            uint32_t api;
        };

        struct EngineContext {

            Rendering::Renderer* renderer = nullptr;
            Rendering::CameraManager* cameraManager = nullptr;

            Resources::ResourcesManager* resourcesManager = nullptr;
            Filesystem::FileManager* fileManager = nullptr;
            Filesystem::AssetIDManager* assetIDManager = nullptr;

            ObjectIDManager* objIDManager = nullptr;

            Levels::LevelManager* levelManager = nullptr;

            Events::EventDispatcher* eventDispatcher = nullptr;

            Audio::AudioManager* audioManager = nullptr;

            Audio::AudioIDManager* audioIDManager = nullptr;

            Physics::PhysicsManager* physicsManager = nullptr;

            Time::TimeManager* timeManager = nullptr;

            Platform::IPlatform* platform = nullptr;

            std::shared_ptr<Projects::Project> currentProject = nullptr;

            Debugging::Profiler* profiler = nullptr;
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
                virtual Rendering::CameraManager* GetCameraManager() const = 0;
                virtual Resources::ResourcesManager* GetResourcesManager() const = 0;
                virtual Filesystem::FileManager* GetFileManager() const = 0;
                virtual Filesystem::AssetIDManager* GetAssetIDManager() const = 0;
                virtual ObjectIDManager* GetObjectIDManager() const = 0;
                virtual Physics::PhysicsManager* GetPhysicsManager() const = 0;
                virtual Levels::LevelManager* GetLevelManager() const = 0;
                virtual Events::EventDispatcher* GetEventDispatcher() const = 0;
                virtual Audio::AudioManager* GetAudioManager() const = 0;
                virtual Audio::AudioIDManager* GetAudioIDManager() const = 0;
                virtual Time::TimeManager* GetTimeManager() const = 0;
                virtual Projects::BuildSettings* GetBuildSettings() const = 0;
                virtual std::shared_ptr<Projects::Project> GetCurrentProject() const = 0;
                virtual Debugging::Profiler* GetProfiler() const = 0;

                virtual bool IsInPlayMode() const = 0;
                virtual void SetPlayMode(bool on) = 0;
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

                Rendering::CameraManager* GetCameraManager() const override { return m_Context.cameraManager; }

                Resources::ResourcesManager* GetResourcesManager() const override { return m_Context.resourcesManager; }

                Filesystem::FileManager* GetFileManager() const override { return m_Context.fileManager; }

                Filesystem::AssetIDManager* GetAssetIDManager() const override { return m_Context.assetIDManager; }

                ObjectIDManager* GetObjectIDManager() const override { return m_Context.objIDManager; }

                Physics::PhysicsManager* GetPhysicsManager() const override { return m_Context.physicsManager; }

                Levels::LevelManager* GetLevelManager() const override { return m_Context.levelManager; }

                Events::EventDispatcher* GetEventDispatcher() const override { return m_Context.eventDispatcher; }

                Audio::AudioManager* GetAudioManager() const override { return m_Context.audioManager; }

                Audio::AudioIDManager* GetAudioIDManager() const override { return m_Context.audioIDManager; }

                Time::TimeManager* GetTimeManager() const override { return m_Context.timeManager; }

                Projects::BuildSettings* GetBuildSettings() const override;

                std::shared_ptr<Projects::Project> GetCurrentProject() const override { return m_Context.currentProject; }

                Debugging::Profiler* GetProfiler() const override { return m_Context.profiler; }

                bool IsInPlayMode() const override { return m_PlayMode; }

                void SetPlayMode(bool on) override;

            private:
                EngineInstance() = default;

                EngineContext m_Context;

                EngineCreationSettings m_EngineSettings;

                bool m_PlayMode = false;
                bool m_ActivateAllPhysics = false;
                bool m_ReloadCurrentLevel = false;
                
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
