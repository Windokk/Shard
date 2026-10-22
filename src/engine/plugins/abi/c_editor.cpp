#include <atomic>
#include <memory>

#include <shard/shard_editor.h>

#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/plugins/context.hpp"

// shard.editor : the panels of a C plugin. The editor owns ImGui : it gives the widgets (a ShardUiApi made of ImGui calls) and the window
// begin / end ; this file wraps the plugin's draw function in them and registers the panel as an "editor.panel" item of the plugin.

namespace Shard::Engine::Plugins {

    namespace {
        std::atomic<const EditorUiBackend*> gBackend{ nullptr };
    }

    void SetEditorUiBackend(const EditorUiBackend* backend) { gBackend.store(backend); }

    namespace CAbi {

        bool EditorUiAvailable()
        {
            const EditorUiBackend* b = gBackend.load();
            return b && b->widgets && b->begin && b->end;
        }

        namespace {
            ShardResult SHARD_CALL AddPanel(ShardPluginCtx handle, ShardStr title, ShardPanelFn draw, void* user)
            {
                try {
                    PluginContext* ctx = FindContext(handle);
                    if (!ctx) return SHARD_E_STALE_HANDLE;
                    if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                    const std::string name = ToString(title);
                    if (name.empty() || !draw) return SHARD_E_INVALID_ARG;
                    if (!EditorUiAvailable()) return SHARD_E_UNSUPPORTED;

                    std::atomic<uint32_t>* active = ActiveCounter(*ctx);
                    auto report = ctx->Bindings().reportError;
                    const PluginOwner owner = PluginOwner(handle);
                    auto item = std::make_shared<EditorPanelItem>();
                    item->title = name;
                    item->draw = [name, draw, user, active, report, owner](bool* open) {
                        const EditorUiBackend* backend = gBackend.load();
                        if (!backend || !backend->widgets || !backend->begin || !backend->end) return;
                        if (backend->begin(name.c_str(), open)) {
                            ActiveScope scope(active, owner);
                            try {
                                draw(user, backend->widgets);
                            } catch (const std::exception& e) {
                                if (report) report(owner, "panel '" + name + "' threw : " + e.what());
                            } catch (...) {
                                if (report) report(owner, "panel '" + name + "' threw");
                            }
                        }
                        backend->end();
                    };
                    return ctx->Assets().Add<EditorPanelItem>("editor.panel", name, std::move(item)) ? SHARD_OK : SHARD_E_ALREADY_EXISTS;
                } catch (...) { return SHARD_E_INTERNAL; }
            }
        }

        const void* EditorApi()
        {
            static const ShardEditorApi api = { sizeof(ShardEditorApi), &AddPanel };
            return &api;
        }
    }
}
