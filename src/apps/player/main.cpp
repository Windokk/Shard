#include "engine/world/engine.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/game_module.hpp"
#include "engine/platform/glfw/glfw_platform.hpp"
#include "apps/player/glfw_window.hpp"

using namespace Shard;
using namespace Shard::Engine;
using namespace Shard::Engine::Core;
using namespace Shard::Engine::Rendering;
using namespace Shard::Engine::Input;
using namespace Shard::Engine::Objects::Components;
using namespace Shard::Engine::Objects;
using namespace Shard::Game;

#include <iostream>

Debugging::Level minDebugLevel = Debugging::Level::Log;
std::string mainModuleLib = "";

EngineCreationSettings ComputeEngineSettings(int argc, char* argv[]) {
    Engine::Core::EngineCreationSettings settings;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            settings.windowWidth = std::stoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            settings.windowHeight = std::stoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--fullscreen") == 0) {
            settings.fullscreen = true;
        }
        else if (strcmp(argv[i], "--vsync") == 0) {
            settings.vsync = false;
        }
        else if (strcmp(argv[i], "--project") == 0 && i + 1 < argc) {
            settings.project = argv[++i];
        }
        else if (strcmp(argv[i], "--gravity") == 0 && i + 3 < argc) {
            float x = std::stof(argv[++i]);
            float y = std::stof(argv[++i]);
            float z = std::stof(argv[++i]);
            settings.gravity = glm::vec3(x, y, z);
            settings.overrideGravity = true;
        }
        else if (strcmp(argv[i], "--debug") == 0 && i + 1 < argc) {
            std::string level = argv[++i];
            if (level == "log") {
                minDebugLevel = Debugging::Level::Log;
            } else if (level == "info") {
                minDebugLevel = Debugging::Level::Info;
            } else if (level == "warning") {
                minDebugLevel = Debugging::Level::Warning;
            } else if (level == "error") {
                minDebugLevel = Debugging::Level::Error;
            } else if (level == "fatal") {
                minDebugLevel = Debugging::Level::Fatal;
            } else {
                std::cerr << "Unknown debug level: " << level << ". Using default (Log).\n";
            }
        }
        else if(strcmp(argv[i], "--game") == 0 && i + 1 < argc){
            mainModuleLib = argv[++i];
        }
        else if(strcmp(argv[i], "--api") == 0 && i + 1 < argc){
            std::string api = argv[++i];
            if(api == "opengl")
                settings.api = 0;
            if(api == "vulkan")
                settings.api = 1;
            if(api == "dx11")
                settings.api = 2;
            if(api == "dx12")
                settings.api = 3;
        }
    }

    return settings;
}

void early_crash(){
    std::cout << "Shard Engine has crashed. Press Enter to exit..." << std::endl;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();
    std::terminate();
}

int main(int argc, char* argv[]) {

    // Engine init parameters
    EngineCreationSettings engineSettings = ComputeEngineSettings(argc, argv);

    if(engineSettings.project == ""){
        std::cerr<<"No project specified, aborting..."<<std::endl;
        early_crash();
    }

    
    EngineInstance* engine = &EngineInstance::GetInstance();

    Engine::Core::SetEngine(engine);
    Engine::Debugging::SetLogger(&Engine::Debugging::Logger::GetInstance());

    //Game module loading and init
    if (!LoadGameModule(mainModuleLib)) {
        early_crash();
    }

    // Platform creation
    engineSettings.platform = new Shard::Engine::Core::Platform::GLFWPlatform<Shard::Game::Core::Platform::GLFWWindow>();

    // Engine startup
    Engine::Core::GetEngine().Init(engineSettings);

    
    Engine::Core::GetEngine().SetPlayMode(true);

    //Main Loop
    while (!Engine::Core::GetEngine().ShouldEnd()) {
        if (!Engine::Core::GetEngine().Run()) break;
    }

    //Cleaning
    Engine::Core::GetEngine().Destroy();

    UnloadGameModule();

    std::cout << "Shard Engine has finished. Press Enter to exit..." << std::endl;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();

    return 0;
}