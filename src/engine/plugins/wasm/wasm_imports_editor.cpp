#include <cstring>

#include <shard/shard_editor.h>

#include "engine/plugins/wasm/wasm_imports.hpp"
#include "engine/plugins/wasm/wasm_marshal.hpp"

// shard.editor for a sandboxed plugin (permission "editor"). What the module adds is added to the editor through the C ABI of the host, with
// trampolines that call the module's functions : a panel, an inspector or a gizmo is called with the table of widgets (or of the gizmo painter)
// that the module built from its own ui.* / gizmo.* imports ; those imports act on what the host is drawing at that moment (Sandbox::ui, ::gizmo).

namespace Shard::Engine::Plugins::Wasm {

    using namespace Marshal;

    namespace {
        constexpr ValType I = ValType::I32, L = ValType::I64;

        struct WasmCallback final : Registration {
            Sandbox* sandbox = nullptr;
            uint32_t fn = 0, user = 0;
        };

        // Sets what the module's ui.* or gizmo.* imports draw with, for the length of a callback
        template <typename T, typename F>
        void WithDrawing(Sandbox& s, const T* Sandbox::*member, const T* table, F&& call)
        {
            const T* previous = s.*member;
            s.*member = table;
            call();
            s.*member = previous;
        }

        void SHARD_CALL PanelTrampoline(void* user, const ShardUiApi* ui)
        {
            auto* c = static_cast<WasmCallback*>(user);
            Sandbox& s = *c->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            if (s.dead || !s.uiTableAt) return;
            static const FuncType type{ { I, I }, {} };
            std::vector<uint64_t> out;
            WithDrawing(s, &Sandbox::ui, ui, [&] { s.CallTable(c->fn, type, { c->user, s.uiTableAt }, Budget::Frame, out); });
        }

        void SHARD_CALL ActionTrampoline(void* user)
        {
            auto* c = static_cast<WasmCallback*>(user);
            Sandbox& s = *c->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            if (s.dead) return;
            static const FuncType type{ { I }, {} };
            std::vector<uint64_t> out;
            s.CallTable(c->fn, type, { c->user }, Budget::Call, out);
        }

        void SHARD_CALL InspectorTrampoline(void* user, const ShardUiApi* ui, ShardActor actor)
        {
            auto* c = static_cast<WasmCallback*>(user);
            Sandbox& s = *c->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            if (s.dead || !s.uiTableAt) return;
            static const FuncType type{ { I, I, L }, {} };
            std::vector<uint64_t> out;
            WithDrawing(s, &Sandbox::ui, ui, [&] { s.CallTable(c->fn, type, { c->user, s.uiTableAt, actor }, Budget::Frame, out); });
        }

        void SHARD_CALL GizmoTrampoline(void* user, const ShardGizmoApi* gizmo, ShardActor actor)
        {
            auto* c = static_cast<WasmCallback*>(user);
            Sandbox& s = *c->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            if (s.dead || !s.gizmoTableAt) return;
            static const FuncType type{ { I, I, L }, {} };
            std::vector<uint64_t> out;
            WithDrawing(s, &Sandbox::gizmo, gizmo, [&] { s.CallTable(c->fn, type, { c->user, s.gizmoTableAt, actor }, Budget::Frame, out); });
        }

        void SHARD_CALL SelectionTrampoline(void* user, const ShardActor* actors, uint32_t count)
        {
            auto* c = static_cast<WasmCallback*>(user);
            Sandbox& s = *c->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            if (s.dead) return;
            const uint32_t at = s.heap.Temp(std::max<uint64_t>(uint64_t(count) * 8, 8), 8);
            if (!at) return;
            if (count) s.instance->Write(at, actors, count * 8);
            static const FuncType type{ { I, I, I }, {} };
            std::vector<uint64_t> out;
            s.CallTable(c->fn, type, { c->user, at, count }, Budget::Call, out);
        }
    }

    void AddEditorImports(Sandbox& s, ImportList& imports, const HostApis& apis)
    {
        const ShardEditorApi* api = apis.editor;
        auto refuse = [](Instance&, const uint64_t*, uint64_t* r) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; };

        if (!api) {
            for (const char* name : { "editor.add_panel", "editor.add_menu_item", "editor.add_toolbar_button", "editor.add_inspector", "editor.add_gizmo",
                                      "editor.subscribe_selection" })
                imports.AddManual(name, refuse);
        } else {
            auto callback = [&s](uint64_t fn, uint64_t user) {
                auto* c = s.Register<WasmCallback>();
                c->sandbox = &s;
                c->fn = uint32_t(fn);
                c->user = uint32_t(user);
                return c;
            };
            // Each of them is the C ABI's own function with a trampoline in place of the module's function pointer
            imports.AddManual("editor.add_panel", [&s, api, callback](Instance& in, const uint64_t* a, uint64_t* r) {
                if (!s.Allowed(Permission::Editor)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
                std::string title;
                if (!ReadStr(in, a[1], title, 256)) return false;
                r[0] = uint32_t(api->add_panel(s.ctx, ToStr(title), &PanelTrampoline, callback(a[2], a[3])));
                return true;
            });
            imports.AddManual("editor.add_menu_item", [&s, api, callback](Instance& in, const uint64_t* a, uint64_t* r) {
                if (!s.Allowed(Permission::Editor)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
                std::string path, shortcut;
                if (!ReadStr(in, a[1], path, 512) || !ReadStr(in, a[2], shortcut, 64)) return false;
                r[0] = uint32_t(api->add_menu_item(s.ctx, ToStr(path), ToStr(shortcut), &ActionTrampoline, callback(a[3], a[4])));
                return true;
            });
            imports.AddManual("editor.add_toolbar_button", [&s, api, callback](Instance& in, const uint64_t* a, uint64_t* r) {
                if (!s.Allowed(Permission::Editor)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
                std::string label, tooltip;
                if (!ReadStr(in, a[1], label, 256) || !ReadStr(in, a[2], tooltip, 1024)) return false;
                r[0] = uint32_t(api->add_toolbar_button(s.ctx, ToStr(label), ToStr(tooltip), &ActionTrampoline, callback(a[3], a[4])));
                return true;
            });
            imports.AddManual("editor.add_inspector", [&s, api, callback](Instance& in, const uint64_t* a, uint64_t* r) {
                if (!s.Allowed(Permission::Editor)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
                std::string type;
                if (!ReadStr(in, a[1], type, 256)) return false;
                r[0] = uint32_t(api->add_inspector(s.ctx, ToStr(type), &InspectorTrampoline, callback(a[2], a[3])));
                return true;
            });
            imports.AddManual("editor.add_gizmo", [&s, api, callback](Instance& in, const uint64_t* a, uint64_t* r) {
                if (!s.Allowed(Permission::Editor)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
                std::string name;
                if (!ReadStr(in, a[1], name, 256)) return false;
                r[0] = uint32_t(api->add_gizmo(s.ctx, ToStr(name), &GizmoTrampoline, callback(a[2], a[3])));
                return true;
            });
            imports.AddManual("editor.subscribe_selection", [&s, api, callback](Instance&, const uint64_t* a, uint64_t* r) {
                if (!s.Allowed(Permission::Editor)) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; }
                r[0] = uint32_t(api->subscribe_selection(s.ctx, &SelectionTrampoline, callback(a[1], a[2])));
                return true;
            });
        }

        // The widget with an array of strings, the only one the generator cannot marshal
        imports.AddManual("ui.combo", [&s](Instance& in, const uint64_t* a, uint64_t* r) {
            r[0] = 0;
            const ShardUiApi* ui = s.ui;
            if (!ui || !SHARD_HAS(ui, combo) || !s.Allowed(Permission::Editor)) return true;
            std::string label;
            if (!ReadStr(in, a[0], label)) return false;
            OutArg<int32_t> current(in, a[1]);
            if (!current.ok) return false;
            const uint32_t count = uint32_t(a[3]);
            if (count > 4096) { in.HostTrap("a combo of more than 4096 items"); return false; }
            std::vector<std::string> names(count);
            std::vector<ShardStr> items(count);
            for (uint32_t i = 0; i < count; ++i) {
                if (!ReadStr(in, a[2] + 16ull * i, names[i], 4096)) return false;
                items[i] = ToStr(names[i]);
            }
            r[0] = uint32_t(ui->combo(ToStr(label), &current.value, items.empty() ? nullptr : items.data(), count));
            return current.Commit();
        });
    }
}
