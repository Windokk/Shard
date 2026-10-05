#pragma once

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOCRYPT
    #define NORPC
    #include <windows.h>
    using ModuleHandle = HMODULE;
#else
    #include <dlfcn.h>
    using ModuleHandle = void*;
#endif

#include <iostream>

#include "engine/world/components/registry/component_registry.hpp"

#include "engine/core/engine.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Core;
using namespace Shard::Engine::Debugging;

namespace Shard::Game {

    //GAME
    using GameInitFn = void(*)(IEngineContext*, Objects::Components::ComponentRegistry*, Logger*);
    using GameRegisterComponentsFn = void(*)();

    class ModuleLoader {
    public:
        static ModuleLoader& GetInstance() {
            static ModuleLoader instance;
            return instance;
        }

        bool LoadModule(const std::string& name, const std::string& path) {
            if (modules.find(name) != modules.end()) return true;

            ModuleHandle handle;

        #if defined(_WIN32)
            handle = LoadLibraryA(path.c_str());
            if (!handle) {
                DWORD err = GetLastError();
                std::cerr<<"Failed to load module: " + path + "  Error code : "+std::to_string(err)<<std::endl;
                return false;
            }
        #else
            handle = dlopen(path.c_str(), RTLD_NOW);
            if (!handle) {
                std::cerr<<"Failed to load module: " + std::string(dlerror())<<std::endl;
                return false;
            }
        #endif

            modules[name] = handle;
            std::cout<<"Module loaded: " + path<<std::endl;
            return true;
        }

        template<typename T>
        T GetSymbol(const std::string& moduleName, const std::string& symbolName) {
            if (modules.find(moduleName) == modules.end()) {
                std::cerr<<"Module not loaded: " + moduleName<<std::endl;
                return nullptr;
            }

            ModuleHandle handle = modules[moduleName];

        #if defined(_WIN32)
            auto symbol = reinterpret_cast<T>(GetProcAddress(handle, symbolName.c_str()));
        #else
            auto symbol = reinterpret_cast<T>(dlsym(handle, symbolName.c_str()));
        #endif

            if (!symbol) {
                std::cerr<<"Symbol not found: " + symbolName<<std::endl;
            }

            return symbol;
        }

        void UnloadModule(const std::string& moduleName) {
            if (modules.find(moduleName) == modules.end()) return;

        #if defined(_WIN32)
            FreeLibrary(modules[moduleName]);
        #else
            dlclose(modules[moduleName]);
        #endif

            modules.erase(moduleName);
            std::cout<<"Unloaded module: " + moduleName<<std::endl;
        }

        bool IsModuleLoaded(const std::string& moduleName) {
            return modules.find(moduleName) != modules.end();
        }

    private:
        std::unordered_map<std::string, ModuleHandle> modules;

        ModuleLoader() = default;
        ~ModuleLoader() = default;
        ModuleLoader(const ModuleLoader&) = delete;
        ModuleLoader& operator=(const ModuleLoader&) = delete;
    };
}