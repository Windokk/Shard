#include "engine/game_module.hpp"

#include <iostream>

#include "engine/platform/module_loader.hpp"
#include "engine/world/components/registry/component_registry.hpp"
#include "engine/world/engine.hpp"
#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine {

    namespace {
        const std::string kGameModuleName = "game";

        using GameInitFn = void(*)(Core::IEngineContext*, Objects::Components::ComponentRegistry*, Debugging::Logger*);
        using GameRegisterComponentsFn = void(*)();
    }

    bool LoadGameModule(const std::string& path)
    {
        auto& loader = Core::Platform::ModuleLoader::GetInstance();

        if (!loader.LoadModule(kGameModuleName, path)) {
            std::cerr << "Failed to load module: game" << std::endl;
            return false;
        }

        auto initGame = loader.GetSymbol<GameInitFn>(kGameModuleName, "InitializeSingletons");
        if (!initGame) {
            std::cerr << "Failed to find symbol: InitializeSingletons" << std::endl;
            return false;
        }

        initGame(&Core::GetEngine(), &Objects::Components::GetComponentRegistry(), &Debugging::GetLogger());

        auto registerGameComponents = loader.GetSymbol<GameRegisterComponentsFn>(kGameModuleName, "RegisterGameComponents");
        if (!registerGameComponents) {
            std::cerr << "Failed to find symbol: RegisterGameComponents" << std::endl;
            return false;
        }
        registerGameComponents();

        return true;
    }

    void UnloadGameModule()
    {
        Core::Platform::ModuleLoader::GetInstance().UnloadModule(kGameModuleName);
    }
}
