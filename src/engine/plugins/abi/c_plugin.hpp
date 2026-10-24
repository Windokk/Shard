#pragma once

#include <memory>
#include <string>
#include <vector>

#include <shard/shard_editor.h>
#include <shard/shard_host.h>

#include "engine/plugins/context.hpp"
#include "engine/plugins/package.hpp"
#include "engine/plugins/plugin.hpp"

// The host side of the C ABI (sdk/include/shard, docs/design-notes/c-abi-sdk.fr.md) : the table a "c1" plugin is given and the IModule that
// stands for it. The manager treats the result like any other plugin ; the registrars are the ones the C++ plugins use.

namespace Shard::Engine::Events { class EventDispatcher; }
namespace Shard::Engine::Objects { class Actor; }

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

    /// What the editor gives the C plugins that have panels (shard.editor): the widgets, drawn with its ImGui, and the window they go in.
    /// `begin` opens the window `title` (false : it is collapsed, draw nothing) and `end` is always called after it.
    struct EditorUiBackend {
        const ShardUiApi* widgets = nullptr;
        bool (*begin)(const char* title, bool* open) = nullptr;
        void (*end)() = nullptr;
        /// A notification in the corner of the editor (SHARD_EDITOR_NOTIFY_*)
        void (*notify)(uint32_t level, const char* text) = nullptr;
        /// The selected actors, the primary one last
        std::vector<std::shared_ptr<Objects::Actor>> (*selection)() = nullptr;
        /// Records an edit a plugin already made in the undo history under `label` (shard_editor.h, undo_push) : `undo` and `redo` are called by the editor's
        /// own undo and redo, in order with its other edits
        void (*pushUndo)(const std::string& label, std::function<void()> undo, std::function<void()> redo) = nullptr;
    };

    /// The editor tells its plugins the selection changed: the C plugins that subscribed (shard.editor) and the EditorPlugins are called
    void NotifyEditorSelection(const std::vector<std::shared_ptr<Objects::Actor>>& selected);
    /// The handle shard_actors.h gives an actor (what an inspector or a gizmo is called with)
    uint64_t EditorActorHandle(const std::shared_ptr<Objects::Actor>& actor);
    /// Called by the editor once its ImGui is up (and with null before it goes): without it, shard.editor is not offered
    void SetEditorUiBackend(const EditorUiBackend* backend);

    /// The native texture id (the graphics API's) of a ShardTexture of shard.render, 0 if it has none (yet). Defined by the engine root, which owns the
    /// renderer ; the editor draws the textures of plugins with it. Call it on the thread that draws.
    uint64_t RenderTextureForImGui(uint64_t texture, bool* flipV);

    /// Delivers the named events C systems emitted during the frame (main thread, end of the frame)
    void FlushCEvents(Events::EventDispatcher* dispatcher);

    /// Start and end of every frame, from the plugin manager (main thread) : what the C ABI's interfaces do once a frame -- the posts of shard.jobs,
    /// the sockets of shard.net, the input events of shard.input, the aging of what shard.debug drew
    void BeginCFrame(uint64_t frame);
    void EndCFrame(uint64_t frame);

    /// Applies the poses C systems set during the phase that just ended to the Transform of their actors (main thread, the scheduler's phase end)
    void FlushCPoses(Core::IEngineContext* engine);
}
