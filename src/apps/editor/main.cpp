#include "engine/core/config/cvar.hpp"
#include <cstdlib>
#include "engine/world/engine.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/game_module.hpp"
#include "engine/plugins/manager.hpp"
#include "imgui/imgui.h"
#include "apps/editor/commands/command_stack.hpp"
#include "engine/platform/windowing/sdl/sdl_platform.hpp"
#include "apps/editor/gui/main_window.hpp"
#include "apps/editor/gui/plugin_ui.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Core;
using namespace Shard::Engine::Rendering;
using namespace Shard::Engine::Input;
using namespace Shard::Engine::Objects::Components;
using namespace Shard::Engine::Objects;

#include <iostream>

Debugging::Level minDebugLevel = Debugging::Level::Log;
std::string gameModuleLib = "";
Shard::Engine::PluginStartupOptions pluginOptions;

int maxFrames = -1;

EngineCreationSettings ComputeEngineSettings(int argc, char* argv[]) {
    {
        std::vector<std::string> cvarWarnings;
        Shard::Engine::Core::CVarRegistry::Global().ParseCommandLine(argc, argv, &cvarWarnings);
        for (const std::string& w : cvarWarnings) std::cerr << "[WARNING] " << w << std::endl;
    }
    Core::EngineCreationSettings settings;

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
        else if (strcmp(argv[i], "--vsync") == 0 && i + 1 < argc) {
            settings.vsync = atoi(argv[++i]) != 0;
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
        else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            settings.scriptFile = argv[++i];
        }
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            maxFrames = std::stoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--game") == 0 && i + 1 < argc) {
            gameModuleLib = argv[++i];
        }
        else if (const int taken = ParsePluginArgument(argc, argv, i, pluginOptions); taken > 0) {
            i += taken - 1;
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
    
    EngineCreationSettings engineSettings = ComputeEngineSettings(argc, argv);
    
    if(engineSettings.project == ""){
        std::cerr<<"No project specified, aborting..."<<std::endl;
        early_crash();
    }

    EngineInstance* engine = &EngineInstance::GetInstance();

    Core::SetEngine(engine);

    Debugging::SetLogger(&Debugging::Logger::GetInstance());

    pluginOptions.host = Plugins::Host::Editor;
    pluginOptions.gameModule = gameModuleLib;
    pluginOptions.project = engineSettings.project;
    pluginOptions.hotReload = true;
    // shard.editor must exist while plugins load (OnLoad adds menus, panels, gizmos). Widgets only run at draw time, so install the backend now and EnsureImGuiInitialized gives it the window later.
    Shard::Editor::GUI::InstallPluginUi(nullptr);
    if (!StartPlugins(pluginOptions)) {
        early_crash();
    }

    {
        engineSettings.platform = new Shard::Engine::Core::Platform::SDLPlatform<Shard::Editor::Core::EditorMainWindow>();

        Core::GetEngine().Init(engineSettings);
        
        Shard::Editor::Commands::CommandStack::Get();
        {
            Plugins::EditorImGui imgui;
            ImGuiMemAllocFunc allocate = nullptr;
            ImGuiMemFreeFunc free = nullptr;
            void* userData = nullptr;
            ImGui::GetAllocatorFunctions(&allocate, &free, &userData);
            imgui.context = ImGui::GetCurrentContext();
            imgui.allocate = reinterpret_cast<void* (*)(size_t, void*)>(allocate);
            imgui.free = reinterpret_cast<void (*)(void*, void*)>(free);
            imgui.userData = userData;
            Plugins::PluginManager::Get().SetEditorImGui(imgui);
        }
        Plugins::PluginManager::Get().NotifyEditorInit();
    }

    int frames = 0;
    while (!Core::GetEngine().ShouldEnd()) {
        if (maxFrames >= 0 && frames >= maxFrames) break;
        if (!Core::GetEngine().Run()) break;
        frames++;

        Core::GetEngine().GetWindow()->ProcessInputs();
        
    }

    Shard::Editor::GUI::EditorResources::Instance().Shutdown();

    Plugins::PluginManager::Get().NotifyEditorShutdown();
    Core::GetEngine().Destroy();
    StopPlugins();

    if (maxFrames >= 0) {
        std::cout << "Rendered " << frames << " frames, shut down cleanly." << std::endl;
        return 0;
    }

    std::cout << "Shard Engine has finished. Press Enter to exit..." << std::endl;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();

    return 0;
}