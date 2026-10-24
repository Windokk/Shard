/* Shard C ABI : every header at once. See README.md next to the include folder for what each one holds. */
#ifndef SHARD_H
#define SHARD_H

#include "shard_abi.h"
#include "shard_host.h"
#include "shard_plugin.h"
#include "shard_cvars.h"
#include "shard_commands.h"
#include "shard_components.h"
#include "shard_actors.h"
#include "shard_scripts.h"
#include "shard_input.h"
#include "shard_physics.h"
#include "shard_audio.h"
#include "shard_worlds.h"
#include "shard_editor.h"
#include "shard_ecs.h"
#include "shard_systems.h"
#include "shard_events.h"
#include "shard_hooks.h"
#include "shard_services.h"
#include "shard_extensions.h"
#include "shard_content.h"
#include "shard_state.h"
#include "shard_experimental.h"

/* Compiled for wasm32 (a sandboxed plugin) : the interfaces are imports of the host, see shard_wasm.h */
#if defined(__wasm__)
#   include "shard_wasm.h"
#endif

#endif /* SHARD_H */
