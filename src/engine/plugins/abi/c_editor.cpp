#include <algorithm>
#include <atomic>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>

#include <shard/shard_editor.h>

#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/plugins/context.hpp"
#include "engine/plugins/manager.hpp"

// shard.editor : what a C plugin adds to the editor. The editor owns ImGui : it gives the widgets (a ShardUiApi made of ImGui calls) and the window
// begin / end ; this file wraps the plugin's functions in them and registers each contribution as an item of an extension point of the plugin
// ("editor.panel", "editor.menu", "editor.toolbar", "editor.inspector", "editor.gizmo"), which the editor reads and which go with the plugin.

namespace Shard::Engine::Plugins {

    namespace {
        std::atomic<const EditorUiBackend*> gBackend{ nullptr };

        struct SelectionSubscription {
            PluginOwner owner = 0;
            ShardSelectionFn fn = nullptr;
            void* user = nullptr;
            std::atomic<uint32_t>* active = nullptr;
            std::function<void(PluginOwner, const std::string&)> report;
        };
        std::mutex gSelectionMutex;
        std::vector<SelectionSubscription> gSelection;
    }

    void SetEditorUiBackend(const EditorUiBackend* backend) { gBackend.store(backend); }

    uint64_t EditorActorHandle(const std::shared_ptr<Objects::Actor>& actor) { return CAbi::ActorHandle(actor); }

    void NotifyEditorSelection(const std::vector<std::shared_ptr<Objects::Actor>>& selected)
    {
        std::vector<ShardActor> handles;
        std::vector<std::string> paths;
        for (const auto& actor : selected) {
            handles.push_back(CAbi::ActorHandle(actor));
            paths.push_back(CAbi::ActorPath(handles.back()));
        }
        std::vector<SelectionSubscription> subscribers;
        {
            std::lock_guard<std::mutex> lock(gSelectionMutex);
            subscribers = gSelection;
        }
        for (const SelectionSubscription& s : subscribers) {
            CAbi::ActiveScope scope(s.active, s.owner);
            try {
                s.fn(s.user, handles.data(), uint32_t(handles.size()));
            } catch (const std::exception& e) {
                if (s.report) s.report(s.owner, std::string("a selection handler threw : ") + e.what());
            } catch (...) {
                if (s.report) s.report(s.owner, "a selection handler threw");
            }
        }
        PluginManager::Get().NotifySelectionChanged(paths);
    }

    namespace CAbi {

        bool EditorUiAvailable()
        {
            const EditorUiBackend* b = gBackend.load();
            return b && b->widgets && b->begin && b->end;
        }

        namespace {
            // The common part of every contribution : the plugin's context, the editor's presence, the main thread
            PluginContext* Contribution(ShardPluginCtx handle, ShardResult& result)
            {
                result = SHARD_OK;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) { result = SHARD_E_STALE_HANDLE; return nullptr; }
                if (!OnMainThread()) { result = SHARD_E_WRONG_THREAD; return nullptr; }
                if (!EditorUiAvailable()) { result = SHARD_E_UNSUPPORTED; return nullptr; }
                return ctx;
            }

            // Runs a function of the plugin for the editor : attributed to the plugin, what it throws reported and counted as its error
            template <typename F>
            void Guarded(std::atomic<uint32_t>* active, PluginOwner owner, const std::function<void(PluginOwner, const std::string&)>& report, const std::string& what, F&& f)
            {
                ActiveScope scope(active, owner);
                try {
                    f();
                } catch (const std::exception& e) {
                    if (report) report(owner, what + " threw : " + e.what());
                } catch (...) {
                    if (report) report(owner, what + " threw");
                }
            }

            ShardResult SHARD_CALL AddPanel(ShardPluginCtx handle, ShardStr title, ShardPanelFn draw, void* user)
            {
                try {
                    ShardResult result;
                    PluginContext* ctx = Contribution(handle, result);
                    if (!ctx) return result;
                    const std::string name = ToString(title);
                    if (name.empty() || !draw) return SHARD_E_INVALID_ARG;

                    std::atomic<uint32_t>* active = ActiveCounter(*ctx);
                    auto report = ctx->Bindings().reportError;
                    const PluginOwner owner = PluginOwner(handle);
                    auto item = std::make_shared<EditorPanelItem>();
                    item->title = name;
                    item->draw = [name, draw, user, active, report, owner](bool* open) {
                        const EditorUiBackend* backend = gBackend.load();
                        if (!backend || !backend->widgets || !backend->begin || !backend->end) return;
                        if (backend->begin(name.c_str(), open))
                            Guarded(active, owner, report, "panel '" + name + "'", [&] { draw(user, backend->widgets); });
                        backend->end();
                    };
                    return ctx->Assets().Add<EditorPanelItem>("editor.panel", name, std::move(item)) ? SHARD_OK : SHARD_E_ALREADY_EXISTS;
                } catch (...) { return SHARD_E_INTERNAL; }
            }

            ShardResult SHARD_CALL AddMenuItem(ShardPluginCtx handle, ShardStr path, ShardStr shortcut, ShardEditorActionFn run, void* user)
            {
                try {
                    ShardResult result;
                    PluginContext* ctx = Contribution(handle, result);
                    if (!ctx) return result;
                    const std::string where = ToString(path);
                    // "Menu/Item" at least, no empty part
                    const size_t slash = where.find('/');
                    if (!run || slash == std::string::npos || slash == 0 || where.back() == '/' || where.find("//") != std::string::npos) return SHARD_E_INVALID_ARG;
                    std::atomic<uint32_t>* active = ActiveCounter(*ctx);
                    auto report = ctx->Bindings().reportError;
                    const PluginOwner owner = PluginOwner(handle);
                    auto item = std::make_shared<EditorMenuItem>();
                    item->path = where;
                    item->shortcut = ToString(shortcut);
                    item->run = [where, run, user, active, report, owner] { Guarded(active, owner, report, "menu item '" + where + "'", [&] { run(user); }); };
                    return ctx->Assets().Add<EditorMenuItem>("editor.menu", where, std::move(item)) ? SHARD_OK : SHARD_E_ALREADY_EXISTS;
                } catch (...) { return SHARD_E_INTERNAL; }
            }

            ShardResult SHARD_CALL AddToolbarButton(ShardPluginCtx handle, ShardStr label, ShardStr tooltip, ShardEditorActionFn run, void* user)
            {
                try {
                    ShardResult result;
                    PluginContext* ctx = Contribution(handle, result);
                    if (!ctx) return result;
                    const std::string name = ToString(label);
                    if (!run || name.empty()) return SHARD_E_INVALID_ARG;
                    std::atomic<uint32_t>* active = ActiveCounter(*ctx);
                    auto report = ctx->Bindings().reportError;
                    const PluginOwner owner = PluginOwner(handle);
                    auto item = std::make_shared<EditorToolbarItem>();
                    item->label = name;
                    item->tooltip = ToString(tooltip);
                    item->run = [name, run, user, active, report, owner] { Guarded(active, owner, report, "toolbar button '" + name + "'", [&] { run(user); }); };
                    return ctx->Assets().Add<EditorToolbarItem>("editor.toolbar", name, std::move(item)) ? SHARD_OK : SHARD_E_ALREADY_EXISTS;
                } catch (...) { return SHARD_E_INTERNAL; }
            }

            ShardResult SHARD_CALL AddInspector(ShardPluginCtx handle, ShardStr type, ShardInspectorFn draw, void* user)
            {
                try {
                    ShardResult result;
                    PluginContext* ctx = Contribution(handle, result);
                    if (!ctx) return result;
                    if (!draw) return SHARD_E_INVALID_ARG;
                    static std::atomic<uint32_t> counter{ 0 };      // several inspectors of one type are allowed : the key only has to be unique
                    const std::string componentType = ToString(type);
                    std::atomic<uint32_t>* active = ActiveCounter(*ctx);
                    auto report = ctx->Bindings().reportError;
                    const PluginOwner owner = PluginOwner(handle);
                    auto item = std::make_shared<EditorInspectorItem>();
                    item->componentType = componentType;
                    item->draw = [componentType, draw, user, active, report, owner](const ShardUiApi* ui, uint64_t actor) {
                        Guarded(active, owner, report, "inspector of '" + componentType + "'", [&] { draw(user, ui, actor); });
                    };
                    return ctx->Assets().Add<EditorInspectorItem>("editor.inspector", componentType + "#" + std::to_string(counter++), std::move(item)) ? SHARD_OK : SHARD_E_ALREADY_EXISTS;
                } catch (...) { return SHARD_E_INTERNAL; }
            }

            ShardResult SHARD_CALL AddGizmo(ShardPluginCtx handle, ShardStr name, ShardGizmoFn draw, void* user)
            {
                try {
                    ShardResult result;
                    PluginContext* ctx = Contribution(handle, result);
                    if (!ctx) return result;
                    const std::string gizmoName = ToString(name);
                    if (!draw || gizmoName.empty()) return SHARD_E_INVALID_ARG;
                    std::atomic<uint32_t>* active = ActiveCounter(*ctx);
                    auto report = ctx->Bindings().reportError;
                    const PluginOwner owner = PluginOwner(handle);
                    auto item = std::make_shared<EditorGizmoItem>();
                    item->name = gizmoName;
                    item->draw = [gizmoName, draw, user, active, report, owner](const ShardGizmoApi* gizmo, uint64_t actor) {
                        Guarded(active, owner, report, "gizmo '" + gizmoName + "'", [&] { draw(user, gizmo, actor); });
                    };
                    return ctx->Assets().Add<EditorGizmoItem>("editor.gizmo", gizmoName, std::move(item)) ? SHARD_OK : SHARD_E_ALREADY_EXISTS;
                } catch (...) { return SHARD_E_INTERNAL; }
            }

            ShardResult SHARD_CALL SubscribeSelection(ShardPluginCtx handle, ShardSelectionFn fn, void* user)
            {
                try {
                    ShardResult result;
                    PluginContext* ctx = Contribution(handle, result);
                    if (!ctx) return result;
                    if (!fn) return SHARD_E_INVALID_ARG;
                    const PluginOwner owner = PluginOwner(handle);
                    bool first = false;
                    {
                        std::lock_guard<std::mutex> lock(gSelectionMutex);
                        first = std::none_of(gSelection.begin(), gSelection.end(), [&](const SelectionSubscription& s) { return s.owner == owner; });
                        gSelection.push_back(SelectionSubscription{ owner, fn, user, ActiveCounter(*ctx), ctx->Bindings().reportError });
                    }
                    // They end with the plugin's context
                    if (first)
                        ctx->OnRelease("editor-selection", [owner] {
                            std::lock_guard<std::mutex> lock(gSelectionMutex);
                            gSelection.erase(std::remove_if(gSelection.begin(), gSelection.end(), [&](const SelectionSubscription& s) { return s.owner == owner; }), gSelection.end());
                        });
                    return SHARD_OK;
                } catch (...) { return SHARD_E_INTERNAL; }
            }

            ShardResult SHARD_CALL GetSelection(ShardActor* out, uint32_t capacity, uint32_t* count)
            {
                try {
                    if (!count || (!out && capacity != 0)) return SHARD_E_INVALID_ARG;
                    if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                    const EditorUiBackend* backend = gBackend.load();
                    if (!backend || !backend->selection) return SHARD_E_UNSUPPORTED;
                    const auto selected = backend->selection();
                    *count = uint32_t(selected.size());
                    for (uint32_t i = 0; i < capacity && i < selected.size(); ++i) out[i] = ActorHandle(selected[i]);
                    return SHARD_OK;
                } catch (...) { return SHARD_E_INTERNAL; }
            }

            ShardResult SHARD_CALL Notify(ShardPluginCtx handle, uint32_t level, ShardStr message)
            {
                try {
                    if (!FindContext(handle)) return SHARD_E_STALE_HANDLE;
                    if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                    const EditorUiBackend* backend = gBackend.load();
                    if (!backend || !backend->notify) return SHARD_E_UNSUPPORTED;
                    backend->notify(std::min<uint32_t>(level, SHARD_EDITOR_NOTIFY_ERROR), ToString(message).c_str());
                    return SHARD_OK;
                } catch (...) { return SHARD_E_INTERNAL; }
            }

            ShardResult SHARD_CALL SetPanelVisible(ShardPluginCtx handle, ShardStr title, int32_t visible)
            {
                try {
                    if (!FindContext(handle)) return SHARD_E_STALE_HANDLE;
                    if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                    const std::string name = ToString(title);
                    for (const auto& item : PluginManager::Get().Extensions().List("editor.panel")) {
                        if (item.owner != PluginOwner(handle) || item.name != name) continue;
                        std::static_pointer_cast<EditorPanelItem>(item.object)->visible = visible != 0;
                        return SHARD_OK;
                    }
                    return SHARD_E_NOT_FOUND;
                } catch (...) { return SHARD_E_INTERNAL; }
            }

            // ---- 1.2 ----

            // Whether a plugin's undo / redo functions may still be called : false once its context released everything
            std::mutex gUndoMutex;
            std::map<PluginOwner, std::shared_ptr<std::atomic<bool>>> gUndoAlive;

            ShardResult SHARD_CALL UndoPush(ShardPluginCtx handle, ShardStr label, ShardUndoFn undo, ShardUndoFn redo, void* user)
            {
                try {
                    ShardResult result;
                    PluginContext* ctx = Contribution(handle, result);
                    if (!ctx) return result;
                    if (!undo || !redo) return SHARD_E_INVALID_ARG;
                    const EditorUiBackend* backend = gBackend.load();
                    if (!backend || !backend->pushUndo) return SHARD_E_UNSUPPORTED;
                    const PluginOwner owner = PluginOwner(handle);
                    std::shared_ptr<std::atomic<bool>> alive;
                    {
                        std::lock_guard<std::mutex> lock(gUndoMutex);
                        auto& slot = gUndoAlive[owner];
                        if (!slot) {
                            slot = std::make_shared<std::atomic<bool>>(true);
                            ctx->OnRelease("editor-undo", [owner] {
                                std::lock_guard<std::mutex> lock(gUndoMutex);
                                auto it = gUndoAlive.find(owner);
                                if (it != gUndoAlive.end()) { *it->second = false; gUndoAlive.erase(it); }
                            });
                        }
                        alive = slot;
                    }
                    std::atomic<uint32_t>* active = ActiveCounter(*ctx);
                    auto report = ctx->Bindings().reportError;
                    auto call = [alive, active, owner, report, user](ShardUndoFn fn, const char* what) {
                        return [=] {
                            if (!alive->load()) return;
                            Guarded(active, owner, report, what, [&] { fn(user); });
                        };
                    };
                    backend->pushUndo(ToString(label), call(undo, "undo"), call(redo, "redo"));
                    return SHARD_OK;
                } catch (...) { return SHARD_E_INTERNAL; }
            }

            ShardResult SHARD_CALL AddAssetOpener(ShardPluginCtx handle, ShardStr extension, ShardOpenAssetFn open, void* user)
            {
                try {
                    ShardResult result;
                    PluginContext* ctx = Contribution(handle, result);
                    if (!ctx) return result;
                    std::string ext = ToString(extension);
                    if (!open || ext.size() < 2 || ext[0] != '.') return SHARD_E_INVALID_ARG;
                    for (char& c : ext) c = char(std::tolower((unsigned char)c));
                    for (const auto& existing : PluginManager::Get().Extensions().Get<EditorAssetOpenerItem>("editor.assetopener"))
                        if (existing->extension == ext) return SHARD_E_ALREADY_EXISTS;
                    std::atomic<uint32_t>* active = ActiveCounter(*ctx);
                    auto report = ctx->Bindings().reportError;
                    const PluginOwner owner = PluginOwner(handle);
                    auto item = std::make_shared<EditorAssetOpenerItem>();
                    item->extension = ext;
                    item->open = [ext, open, user, active, report, owner](const std::string& path) {
                        Guarded(active, owner, report, "asset opener of '" + ext + "'", [&] { open(user, ToStr(path)); });
                    };
                    return ctx->Assets().Add<EditorAssetOpenerItem>("editor.assetopener", ext, std::move(item)) ? SHARD_OK : SHARD_E_ALREADY_EXISTS;
                } catch (...) { return SHARD_E_INTERNAL; }
            }

            ShardResult SHARD_CALL AddAssetTemplate(ShardPluginCtx handle, ShardStr label, ShardStr defaultName, const void* data, uint64_t size)
            {
                try {
                    ShardResult result;
                    PluginContext* ctx = Contribution(handle, result);
                    if (!ctx) return result;
                    const std::string name = ToString(label), file = ToString(defaultName);
                    if (name.empty() || file.empty() || file.find('/') != std::string::npos || file.find('\\') != std::string::npos || file.find("..") != std::string::npos || (!data && size)) return SHARD_E_INVALID_ARG;
                    auto item = std::make_shared<EditorAssetTemplateItem>();
                    item->label = name;
                    item->defaultName = file;
                    if (size) item->bytes.assign(static_cast<const char*>(data), size_t(size));
                    return ctx->Assets().Add<EditorAssetTemplateItem>("editor.assettemplate", name, std::move(item)) ? SHARD_OK : SHARD_E_ALREADY_EXISTS;
                } catch (...) { return SHARD_E_INTERNAL; }
            }
        }

        const void* EditorApi()
        {
            static const ShardEditorApi api = { sizeof(ShardEditorApi), &AddPanel, &AddMenuItem, &AddToolbarButton, &AddInspector, &AddGizmo,
                                                &SubscribeSelection, &GetSelection, &Notify, &SetPanelVisible, &UndoPush, &AddAssetOpener, &AddAssetTemplate };
            return &api;
        }
    }
}
