// Shard C++ SDK, the gameplay layer: everything a game's source files include.
//
//   #include <shard/game.hpp>
//
//   class Health : public Shard::Script { ... };          // shard/cpp/script.hpp ; its fields are declared in the class with SHARD_FIELD
//   SHARD_REGISTER_SCRIPT(Health)
//
// A game is a folder of such files and one line, in any of them, that makes the plugin:
//
//   SHARD_GAME_MODULE()
//
// (or, when the plugin needs OnLoad / OnEnable code of its own, a class derived from Shard::Plugin and SHARD_C_PLUGIN(YourClass)). The scripts,
// components and systems of every file register themselves.
#pragma once

#include <shard/cpp/actor.hpp>
#include <shard/cpp/audio.hpp>
#include <shard/cpp/component.hpp>
#include <shard/cpp/editor.hpp>
#include <shard/cpp/engine_components.hpp>
#include <shard/cpp/field.hpp>
#include <shard/cpp/input.hpp>
#include <shard/cpp/math.hpp>
#include <shard/cpp/physics.hpp>
#include <shard/cpp/plugin.hpp>
#include <shard/cpp/random.hpp>
#include <shard/cpp/registry.hpp>
#include <shard/cpp/script.hpp>
#include <shard/cpp/system.hpp>
#include <shard/cpp/worlds.hpp>
