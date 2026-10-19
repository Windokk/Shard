#include "engine/game_module.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "engine/core/diagnostics/logger.hpp"
#include "engine/plugins/manager.hpp"

namespace Shard::Engine {

    bool StartPlugins(const PluginStartupOptions& options)
    {
        Plugins::PluginManagerConfig config;
        config.host = options.host;
        config.gameLibrary = options.gameModule;
        config.projectFile = options.project;
        config.pluginPaths = options.pluginPaths;
        config.searchPaths = options.searchPaths;
        config.disabled = options.disabled;
        config.deterministic = options.deterministic;
        config.readOnly = options.readOnly;
        config.strictLock = options.strictLock;
        config.allowHotReload = options.hotReload && !options.deterministic;
        config.ignoreSafeMode = options.ignoreSafeMode;
        config.registries = options.registries;
        config.trustFiles = options.trustFiles;
        config.allowUnsigned = options.allowUnsigned;
        if (const char* env = std::getenv("SHARD_PLUGIN_REGISTRY"))
            if (*env) config.registries.push_back(env);

        if (const char* env = std::getenv("SHARD_PLUGIN_PATH")) {
#if defined(_WIN32)
            const char separator = ';';
#else
            const char separator = ':';
#endif
            std::string all = env;
            size_t start = 0;
            while (start <= all.size()) {
                const size_t at = all.find(separator, start);
                const std::string part = all.substr(start, at == std::string::npos ? std::string::npos : at - start);
                if (!part.empty()) config.searchPaths.push_back(part);
                if (at == std::string::npos) break;
                start = at + 1;
            }
        }

        std::string error;
        if (!Plugins::PluginManager::Get().Startup(config, &error)) {
            std::cerr << "Plugins could not start :\n" << error << std::endl;
            return false;
        }
        return true;
    }

    void StopPlugins()
    {
        Plugins::PluginManager::Get().Shutdown();
    }

    int ParsePluginArgument(int argc, char** argv, int index, PluginStartupOptions& options)
    {
        auto value = [&](const char* name) -> const char* {
            if (index + 1 >= argc) {
                std::cerr << name << " needs a value" << std::endl;
                return nullptr;
            }
            return argv[index + 1];
        };
        const char* arg = argv[index];
        if (std::strcmp(arg, "--plugin") == 0) {
            if (const char* v = value("--plugin")) { options.pluginPaths.push_back(v); return 2; }
            return 1;
        }
        if (std::strcmp(arg, "--plugin-path") == 0) {
            if (const char* v = value("--plugin-path")) { options.searchPaths.push_back(v); return 2; }
            return 1;
        }
        if (std::strcmp(arg, "--disable-plugin") == 0) {
            if (const char* v = value("--disable-plugin")) { options.disabled.insert(v); return 2; }
            return 1;
        }
        if (std::strcmp(arg, "--registry") == 0) {
            if (const char* v = value("--registry")) { options.registries.push_back(v); return 2; }
            return 1;
        }
        if (std::strcmp(arg, "--trust-file") == 0) {
            if (const char* v = value("--trust-file")) { options.trustFiles.push_back(v); return 2; }
            return 1;
        }
        if (std::strcmp(arg, "--allow-unsigned-plugins") == 0) { options.allowUnsigned = true; return 1; }
        if (std::strcmp(arg, "--hot-reload-plugins") == 0) { options.hotReload = true; return 1; }
        if (std::strcmp(arg, "--ignore-safe-mode") == 0) { options.ignoreSafeMode = true; return 1; }
        if (std::strcmp(arg, "--strict-plugin-lock") == 0) { options.strictLock = true; return 1; }
        return 0;
    }

    bool LoadGameModule(const std::string& path)
    {
        PluginStartupOptions options;
        options.gameModule = path;
        return StartPlugins(options);
    }

    void UnloadGameModule()
    {
        StopPlugins();
    }
}
