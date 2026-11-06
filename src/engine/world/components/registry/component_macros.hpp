#pragma once

#include "engine/world/components/component.hpp"
#include "component_registry.hpp"
#include "component_registration.hpp"

using namespace Shard::Engine::Objects;
using namespace Shard::Engine::Core;

#define DECLARE_COMPONENT(className)                                                                \
    inline std::shared_ptr<Components::Component> Create_##className() { return Object::Create<className>(nullptr, 0); }    

#define REGISTER_COMPONENT(className)                       \
namespace {                                                 \
    struct AutoRegister_##className {                       \
        AutoRegister_##className() {                        \
            Shard::Engine::Objects::Components::AddComponentRegistrar( \
                [](Shard::Engine::Objects::Components::ComponentRegistry& reg) { \
                    reg.RegisterComponentType(#className, Create_##className); \
                }                                           \
            );                                              \
        }                                                   \
    };                                                      \
    static AutoRegister_##className autoRegister_##className; \
}
