#pragma once

#include <gtest/gtest.h>

#include "engine/world/engine.hpp"
#include "engine/world/objectID.hpp"
#include "engine/world/event_system.hpp"

// Minimal IEngineContext usable from tests without a real window/GL/audio/physics
// bootstrap. Only ObjectIDManager and EventDispatcher are backed by real instances,
// since those are the only subsystems Objects object/component construction depends on
// when no World is attached. Every other accessor aborts the test loudly instead of
// silently returning nullptr, so a test that reaches into an unsupported subsystem
// fails fast with a clear message rather than crashing on a null deref.
namespace Shard::Tests {

    class TestEngineContext : public Shard::Engine::Core::IEngineContext {
        public:
            Shard::Engine::Core::ObjectIDManager* GetObjectIDManager() const override {
                return const_cast<Shard::Engine::Core::ObjectIDManager*>(&objectIDManager);
            }

            Shard::Engine::Events::EventDispatcher* GetEventDispatcher() const override {
                return const_cast<Shard::Engine::Events::EventDispatcher*>(&eventDispatcher);
            }

            bool ShouldEnd() override { Unsupported("ShouldEnd"); return true; }
            void Destroy() override { Unsupported("Destroy"); }
            bool Run() override { Unsupported("Run"); return false; }
            void Init(Shard::Engine::Core::EngineCreationSettings) override { Unsupported("Init"); }
            void InitSystems() override { Unsupported("InitSystems"); }

            void SetSettings(const Shard::Engine::Core::EngineCreationSettings&) override { Unsupported("SetSettings"); }
            Shard::Engine::Core::EngineCreationSettings GetSettings() const override {
                Unsupported("GetSettings");
                return {};
            }

            Shard::Engine::Core::Platform::IWindow* GetWindow() const override { Unsupported("GetWindow"); return nullptr; }
            Shard::Engine::Core::Platform::IInput* GetInputManager() const override { Unsupported("GetInputManager"); return nullptr; }

            Shard::Engine::Rendering::Renderer* GetRenderer() const override { Unsupported("GetRenderer"); return nullptr; }
            Shard::Engine::Rendering::IRenderContext* GetRenderContext() const override { Unsupported("GetRenderContext"); return nullptr; }
            Shard::Engine::Rendering::CameraManager* GetCameraManager() const override { Unsupported("GetCameraManager"); return nullptr; }
            Shard::Engine::Core::Resources::ResourcesManager* GetResourcesManager() const override { Unsupported("GetResourcesManager"); return nullptr; }
            Shard::Engine::Filesystem::FileManager* GetFileManager() const override { Unsupported("GetFileManager"); return nullptr; }
            Shard::Engine::Filesystem::AssetIDManager* GetAssetIDManager() const override { Unsupported("GetAssetIDManager"); return nullptr; }
            Shard::Engine::Physics::PhysicsManager* GetPhysicsManager() const override { Unsupported("GetPhysicsManager"); return nullptr; }
            Shard::Engine::Worlds::WorldManager* GetWorldManager() const override { Unsupported("GetWorldManager"); return nullptr; }
            Shard::Engine::Audio::AudioManager* GetAudioManager() const override { Unsupported("GetAudioManager"); return nullptr; }
            Shard::Engine::Audio::AudioIDManager* GetAudioIDManager() const override { Unsupported("GetAudioIDManager"); return nullptr; }
            Shard::Engine::Time::TimeManager* GetTimeManager() const override { Unsupported("GetTimeManager"); return nullptr; }
            Shard::Engine::Projects::BuildSettings* GetBuildSettings() const override { Unsupported("GetBuildSettings"); return nullptr; }
            std::shared_ptr<Shard::Engine::Projects::Project> GetCurrentProject() const override { Unsupported("GetCurrentProject"); return nullptr; }
            Shard::Engine::Debugging::Profiler* GetProfiler() const override { Unsupported("GetProfiler"); return nullptr; }

            bool IsInPlayMode() const override { return false; }
            void SetPlayMode(bool) override { Unsupported("SetPlayMode"); }

        private:
            static void Unsupported(const char* method) {
                ADD_FAILURE() << "TestEngineContext::" << method
                              << " was called but is not backed by a real subsystem. "
                                 "This test reaches beyond ObjectIDManager/EventDispatcher; "
                                 "either scope the test to those, or extend TestEngineContext.";
            }

            mutable Shard::Engine::Core::ObjectIDManager objectIDManager;
            mutable Shard::Engine::Events::EventDispatcher eventDispatcher;
    };

}
