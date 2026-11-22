#pragma once

#include <nlohmann/json.hpp>

#include "engine/core/config/cvar.hpp"
#include "engine/world/event_system.hpp"

namespace Shard::Engine::Objects::Components {

    nlohmann::ordered_json DescribeCommands(const Core::CVarRegistry& registry);

    nlohmann::ordered_json DescribeEvents(const Events::EventDispatcher& dispatcher);
}
