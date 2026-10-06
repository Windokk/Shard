#pragma once

#include <iostream>
#include <string>
#include <unordered_map>

#include "engine/platform/lib_loader/dynlib.hpp"

namespace Shard::Engine::Core::Platform {

    /// @brief Loads shared libraries (the game's module) and finds their symbols
    class ModuleLoader {
    public:
        static ModuleLoader& GetInstance() {
            static ModuleLoader instance;
            return instance;
        }

        bool LoadModule(const std::string& name, const std::string& path) {
            if (modules.find(name) != modules.end()) return true;

            DynLib library;
            if (!library.Open(path)) {
                std::cerr<<"Failed to load module: " + path + "  Error : " + library.LastError()<<std::endl;
                return false;
            }

            modules.emplace(name, std::move(library));
            std::cout<<"Module loaded: " + path<<std::endl;
            return true;
        }

        template<typename T>
        T GetSymbol(const std::string& moduleName, const std::string& symbolName) {
            auto it = modules.find(moduleName);
            if (it == modules.end()) {
                std::cerr<<"Module not loaded: " + moduleName<<std::endl;
                return nullptr;
            }

            auto symbol = it->second.Symbol<T>(symbolName);
            if (!symbol) {
                std::cerr<<"Symbol not found: " + symbolName<<std::endl;
            }

            return symbol;
        }

        void UnloadModule(const std::string& moduleName) {
            if (modules.erase(moduleName) == 0) return;
            std::cout<<"Unloaded module: " + moduleName<<std::endl;
        }

        bool IsModuleLoaded(const std::string& moduleName) {
            return modules.find(moduleName) != modules.end();
        }

    private:
        std::unordered_map<std::string, DynLib> modules;

        ModuleLoader() = default;
        ~ModuleLoader() = default;
        ModuleLoader(const ModuleLoader&) = delete;
        ModuleLoader& operator=(const ModuleLoader&) = delete;
    };
}
