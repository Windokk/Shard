#include "engine/world/components/registry/component_registration.hpp"
#include "engine/world/engine.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Memory.h>

#include "character.reflection.hpp"
#include "random_kicker.reflection.hpp"
#include "gun.reflection.hpp"

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

    // The module has its own copy of Jolt's statics : without this its allocator hooks stay null, and creating a shape
    // (JPH operator new) from game code jumps to address 0
    JPH::RegisterDefaultAllocator();
}

extern "C" API_EXPORT void RegisterGameComponents() {
    for (auto& cb : GetComponentRegistrars()) {
        cb(GetComponentRegistry());
    }
}
