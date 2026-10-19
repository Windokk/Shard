#include "engine/plugins/context.hpp"
#include "engine/plugins/plugin.hpp"
#include "engine/world/engine.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Memory.h>

#include "character.reflection.hpp"
#include "random_kicker.reflection.hpp"
#include "gun.reflection.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;

/// The game, as a module : its components are the ones the REGISTER_COMPONENT macros declared in the files of this library
class GamePlugin final : public IModule {
public:
    ModuleKind Kind() const override { return ModuleKind::Code; }

    void OnLoad(PluginContext& ctx) override {
        ctx.Components().AdoptStaticRegistrars();
    }
};

// This library has its own copy of Jolt's statics : without this its allocator hooks stay null, and creating a shape
// (JPH operator new) from game code jumps to address 0
static void LinkJolt() {
    JPH::RegisterDefaultAllocator();
}

SHARD_PLUGIN_WITH_LINK(GamePlugin, &LinkJolt)
