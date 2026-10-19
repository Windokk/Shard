#pragma once

#include <set>
#include <string>
#include <vector>

#include "engine/plugins/manifest.hpp"

namespace Shard::Engine {

    /// What an executable tells the plugin system when it starts (see Plugins::PluginManager)
    struct PluginStartupOptions {
        Plugins::Host host = Plugins::Host::Player;
        /// --game : the game's library
        std::string gameModule;
        /// --project : the project file, whose "plugins" list and plugins folder are read
        std::string project;
        /// --plugin (repeatable) : a plugin folder, a manifest, or a library
        std::vector<std::string> pluginPaths;
        /// Extra folders that hold plugins. The folders of the SHARD_PLUGIN_PATH environment variable are added.
        std::vector<std::string> searchPaths;
        /// --disable-plugin (repeatable)
        std::set<std::string> disabled;
        bool deterministic = false;
        bool readOnly = false;
        bool strictLock = false;
        bool hotReload = false;
        bool ignoreSafeMode = false;
        /// --registry (repeatable) : registries for plugin.install / plugin.search (SHARD_PLUGIN_REGISTRY and the project's "registries" are added)
        std::vector<std::string> registries;
        /// --trust-file (repeatable) : files of trusted publishers
        std::vector<std::string> trustFiles;
        /// --allow-unsigned-plugins : load native plugins (tier 1) and install from registries without a trusted signature
        bool allowUnsigned = false;
    };

    /// @brief Finds, loads and initializes the plugins (the game's module among them). Call it once Core::SetEngine() and
    /// Debugging::SetLogger() were done, before the engine starts.
    /// @return false (logged) if a plugin the project requires could not load
    bool StartPlugins(const PluginStartupOptions& options);

    /// Stops the plugins and unloads their libraries. Call after the engine is destroyed.
    void StopPlugins();

    /// Reads the plugin options that all executables share out of the command line (--game and --project are left to the executable) :
    ///   --plugin <path>   --plugin-path <folder>   --disable-plugin <id>   --hot-reload-plugins   --ignore-safe-mode   --strict-plugin-lock
    ///   --registry <source>   --trust-file <path>   --allow-unsigned-plugins
    /// @return how many arguments were taken
    int ParsePluginArgument(int argc, char** argv, int index, PluginStartupOptions& options);

    /// Deprecated : StartPlugins with only the game's library, on the Player host.
    bool LoadGameModule(const std::string& path);
    void UnloadGameModule();
}
