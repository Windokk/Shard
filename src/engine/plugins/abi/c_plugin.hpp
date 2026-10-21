#pragma once

#include <string>

#include <shard/shard_host.h>

#include "engine/plugins/context.hpp"
#include "engine/plugins/plugin.hpp"

// The host side of the C ABI (sdk/include/shard, docs/design-notes/c-abi-sdk.fr.md) : the table a "c1" plugin is given and the IModule that
// stands for it. The manager treats the result like any other plugin ; the registrars are the ones the C++ plugins use.

namespace Shard::Engine::Plugins {

    /// The one static table every C plugin gets. Every entry catches what the engine throws : nothing crosses the boundary.
    const ShardHostApi* GetCHostApi();

    /// Checks what a plugin's entry point filled in (versions, sizes, pointers). "" : fine, otherwise the reason it is refused.
    std::string ValidateCPluginInfo(const ShardPluginInfo& info);

    /// IModule over the callbacks of a C plugin. `info` is copied (the callbacks table is copied up to its own structSize).
    IPlugin* CreateCPlugin(PluginOwner owner, const ShardPluginInfo& info);
}
