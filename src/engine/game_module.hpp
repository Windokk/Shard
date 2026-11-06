#pragma once

#include <set>
#include <string>
#include <vector>

#include "engine/plugins/manifest.hpp"

namespace Shard::Engine {

    struct PluginStartupOptions {
        Plugins::Host host = Plugins::Host::Player;
        std::string gameModule;
        std::string project;
        std::vector<std::string> pluginPaths;
        std::vector<std::string> searchPaths;
        std::set<std::string> disabled;
        bool deterministic = false;
        bool readOnly = false;
        bool strictLock = false;
        bool hotReload = false;
        bool ignoreSafeMode = false;
        std::vector<std::string> registries;
        std::vector<std::string> trustFiles;
        bool allowUnsigned = false;
    };

    bool StartPlugins(const PluginStartupOptions& options);

    void StopPlugins();

    int ParsePluginArgument(int argc, char** argv, int index, PluginStartupOptions& options);

    bool LoadGameModule(const std::string& path);
    void UnloadGameModule();
}
