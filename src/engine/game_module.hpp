#pragma once

#include <string>

namespace Shard::Engine {

    /// @brief Loads the game's shared library, gives it the engine, the component registry and the logger (the
    /// library has its own copy of the engine's singletons) and lets it register its components.
    /// Call it once Core::SetEngine() and Debugging::SetLogger() were done.
    /// @return false (logged) if the library or one of its entry points is missing
    bool LoadGameModule(const std::string& path);

    void UnloadGameModule();
}
