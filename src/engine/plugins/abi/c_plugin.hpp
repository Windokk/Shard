#pragma once

#include <string>

#include <shard/shard_host.h>

#include "engine/plugins/context.hpp"
#include "engine/plugins/package.hpp"
#include "engine/plugins/plugin.hpp"

// The host side of the C ABI (sdk/include/shard, docs/design-notes/c-abi-sdk.fr.md) : the table a "c1" plugin is given and the IModule that
// stands for it. The manager treats the result like any other plugin ; the registrars are the ones the C++ plugins use.

namespace Shard::Engine::Events { class EventDispatcher; }

namespace Shard::Engine::Plugins {

    /// The one static table every C plugin gets. Every entry catches what the engine throws : nothing crosses the boundary.
    const ShardHostApi* GetCHostApi();

    /// Checks what a plugin's entry point filled in (versions, sizes, pointers). "" : fine, otherwise the reason it is refused.
    /// `hostMinor` : the ABI revision the host offers (this engine's, unless a test plays an older host).
    std::string ValidateCPluginInfo(const ShardPluginInfo& info, uint32_t hostMinor = SHARD_ABI_MINOR);

    /// IModule over the callbacks of a C plugin. `info` is copied (the callbacks table is copied up to its own structSize).
    IPlugin* CreateCPlugin(PluginOwner owner, const ShardPluginInfo& info);

    /// The other kinds of plugin a C plugin also is, depending on the callbacks it fills (null : not that kind). They live as long as the plugin.
    struct CPluginFacets {
        EnginePlugin* engine = nullptr;     ///< on_frame_begin / on_frame_end
        ToolPlugin* tool = nullptr;         ///< on_tool_task
    };
    CPluginFacets GetCPluginFacets(IPlugin* plugin);

    /// @brief Whether a plugin may use the "shard.experimental.*" interfaces : every one but a native plugin carrying a valid signature of a
    /// trusted publisher (a plugin that is distributed must not depend on what may change)
    bool MayUseExperimental(TrustTier tier, SignatureInfo::Status status);

    /// Delivers the named events C systems emitted during the frame (main thread, end of the frame)
    void FlushCEvents(Events::EventDispatcher* dispatcher);
}
