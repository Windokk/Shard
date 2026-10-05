#include "engine/world/components/registry/component_registration.hpp"
#include "engine/world/engine.hpp"

#include "character.reflection.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Core;
using namespace Shard::Engine::Objects::Components;
using namespace Shard::Engine::Debugging;

#if defined(_WIN32) || defined(_WIN64)
#   define API_EXPORT __declspec(dllexport)
#else
#   define API_EXPORT __attribute__((visibility("default")))
#endif

extern "C" API_EXPORT void InitializeSingletons(Core::IEngineContext* engine,
                                                            ComponentRegistry* compReg, Logger* logger) {
    SetEngine(engine);
    SetComponentRegistry(compReg);
    SetLogger(logger);
}

extern "C" API_EXPORT void RegisterGameComponents() {
    for (auto& cb : GetComponentRegistrars()) {
        cb(GetComponentRegistry());
    }
}
