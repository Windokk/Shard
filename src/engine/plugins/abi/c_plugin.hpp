#pragma once

#include <memory>
#include <string>
#include <vector>

#include <shard/shard_editor.h>
#include <shard/shard_host.h>

#include "engine/plugins/context.hpp"
#include "engine/plugins/package.hpp"
#include "engine/plugins/plugin.hpp"

namespace Shard::Engine::Events { class EventDispatcher; }
namespace Shard::Engine::Objects { class Actor; }

namespace Shard::Engine::Plugins {

    // Every entry catches engine exceptions, nothing crosses the boundary
    const ShardHostApi* GetCHostApi();

    std::string ValidateCPluginInfo(const ShardPluginInfo& info, uint32_t hostMinor = SHARD_ABI_MINOR);

    IPlugin* CreateCPlugin(PluginOwner owner, const ShardPluginInfo& info);

    struct CPluginFacets {
        EnginePlugin* engine = nullptr;
        ToolPlugin* tool = nullptr;
    };
    CPluginFacets GetCPluginFacets(IPlugin* plugin);

    // Only a native plugin with a valid trusted-publisher signature is kept off the experimental interfaces
    bool MayUseExperimental(TrustTier tier, SignatureInfo::Status status);

    struct EditorUiBackend {
        const ShardUiApi* widgets = nullptr;
        bool (*begin)(const char* title, bool* open) = nullptr;
        void (*end)() = nullptr;
        void (*notify)(uint32_t level, const char* text) = nullptr;
        std::vector<std::shared_ptr<Objects::Actor>> (*selection)() = nullptr;
        void (*pushUndo)(const std::string& label, std::function<void()> undo, std::function<void()> redo) = nullptr;
    };

    void NotifyEditorSelection(const std::vector<std::shared_ptr<Objects::Actor>>& selected);
    uint64_t EditorActorHandle(const std::shared_ptr<Objects::Actor>& actor);
    void SetEditorUiBackend(const EditorUiBackend* backend);

    // Call on the thread that draws
    uint64_t RenderTextureForImGui(uint64_t texture, bool* flipV);

    void FlushCEvents(Events::EventDispatcher* dispatcher);

    void BeginCFrame(uint64_t frame);
    void EndCFrame(uint64_t frame);

    void FlushCPoses(Core::IEngineContext* engine);
}
