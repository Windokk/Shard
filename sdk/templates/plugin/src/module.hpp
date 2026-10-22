// What the files of the game share with the module. Add your own headers next to this one: any .cpp under src/ is part of the plugin.
#pragma once

#include <shard/game.hpp>

/// The console variable "<plugin id>.scale" (created by the module): multiplies every speed
Shard::CVar<float>& SpinScale();
