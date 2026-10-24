#pragma once

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <shard/shard_editor.h>

#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/world/actor.hpp"

// An editor that records what the plugins draw : a ShardUiApi and a ShardGizmoApi whose every function logs its name (and answers what a user who
// clicks everything would), and the backend that goes with them. Used by the tests of the native and of the sandboxed game.

namespace EditorFakes {

    inline std::vector<std::string>& Log() { static std::vector<std::string> log; return log; }
    inline int Count(const std::string& name)
    {
        int n = 0;
        for (const std::string& s : Log()) n += s == name;
        return n;
    }

    inline std::vector<std::pair<uint32_t, std::string>>& Notified() { static std::vector<std::pair<uint32_t, std::string>> v; return v; }
    inline std::vector<std::shared_ptr<Shard::Engine::Objects::Actor>>& Selected() { static std::vector<std::shared_ptr<Shard::Engine::Objects::Actor>> v; return v; }

    inline const ShardUiApi* Widgets()
    {
        static const ShardUiApi ui = [] {
            ShardUiApi u;
            std::memset(&u, 0, sizeof(u));
            u.structSize = sizeof(u);
#define V(name, ...) u.name = [](__VA_ARGS__) { Log().push_back(#name); }
#define B(name, result, ...) u.name = [](__VA_ARGS__) -> int32_t { Log().push_back(#name); return result; }
            V(text, ShardStr); V(text_colored, const float*, ShardStr); V(text_wrapped, ShardStr); V(bullet_text, ShardStr); V(label_text, ShardStr, ShardStr);
            B(button, 0, ShardStr); B(small_button, 0, ShardStr); B(checkbox, 0, ShardStr, int32_t*); B(selectable, 0, ShardStr, int32_t);
            u.combo = [](ShardStr, int32_t* current, const ShardStr* items, uint32_t count) -> int32_t {
                Log().push_back("combo");
                if (count > 2 && items[2].size == 1) *current = 2;
                return SHARD_TRUE;
            };
            B(slider_float, 0, ShardStr, float*, float, float); B(slider_int, 0, ShardStr, int32_t*, int32_t, int32_t);
            B(drag_float, 0, ShardStr, float*, float, float, float); B(drag_int, 0, ShardStr, int32_t*, float, int32_t, int32_t);
            B(input_float, 0, ShardStr, float*); B(input_int, 0, ShardStr, int32_t*); B(input_text, 0, ShardStr, char*, uint64_t); B(color_edit, 0, ShardStr, float*);
            V(progress_bar, float, ShardStr); V(plot_lines, ShardStr, const float*, int32_t);
            V(separator); V(same_line); V(spacing); V(set_next_item_width, float);
            B(collapsing_header, 1, ShardStr); B(tree_node, 0, ShardStr); V(tree_pop); V(push_id, ShardStr); V(pop_id);
            B(is_item_hovered, 0); V(tooltip, ShardStr);
            V(text_disabled, ShardStr); V(separator_text, ShardStr); B(radio_button, 0, ShardStr, int32_t*, int32_t);
            B(drag_float2, 0, ShardStr, float*, float, float, float); B(drag_float3, 0, ShardStr, float*, float, float, float);
            B(drag_float4, 0, ShardStr, float*, float, float, float); B(color_edit3, 0, ShardStr, float*);
            B(input_text_multiline, 0, ShardStr, char*, uint64_t, float, float); B(button_size, 0, ShardStr, float, float);
            V(plot_histogram, ShardStr, const float*, int32_t);
            V(begin_group); V(end_group); V(indent, float); V(unindent, float); V(dummy, float, float); V(new_line); V(push_item_width, float); V(pop_item_width);
            B(begin_child, 1, ShardStr, float, float, int32_t); V(end_child); V(begin_disabled, int32_t); V(end_disabled);
            V(push_text_color, const float*); V(pop_text_color);
            u.content_width = []() -> float { Log().push_back("content_width"); return 123.0f; };
            B(begin_tab_bar, 1, ShardStr); V(end_tab_bar); B(begin_tab_item, 1, ShardStr); V(end_tab_item);
            B(begin_table, 1, ShardStr, int32_t); V(end_table); V(table_setup_column, ShardStr); V(table_headers_row); V(table_next_row); B(table_next_column, 1);
            B(begin_menu, 0, ShardStr); V(end_menu); B(menu_item, 1, ShardStr, ShardStr, int32_t, int32_t); V(open_popup, ShardStr); B(begin_popup, 0, ShardStr);
            V(end_popup); V(close_current_popup); B(begin_popup_context_item, 1);
            B(is_item_clicked, 0, int32_t); B(is_item_active, 0); B(is_item_double_clicked, 0);
#undef V
#undef B
            return u;
        }();
        return &ui;
    }

    inline const ShardGizmoApi* Gizmos()
    {
        static const ShardGizmoApi g = [] {
            ShardGizmoApi t;
            std::memset(&t, 0, sizeof(t));
            t.structSize = sizeof(t);
            t.line = [](const float*, const float*, const float*) { Log().push_back("gizmo.line"); };
            t.point = [](const float*, float, const float*) { Log().push_back("gizmo.point"); };
            t.circle = [](const float*, const float*, float, const float*) { Log().push_back("gizmo.circle"); };
            t.sphere = [](const float*, float, const float*) { Log().push_back("gizmo.sphere"); };
            t.box = [](const float*, const float*, const float*) { Log().push_back("gizmo.box"); };
            t.arrow = [](const float*, const float*, const float*) { Log().push_back("gizmo.arrow"); };
            t.text = [](const float*, ShardStr, const float*) { Log().push_back("gizmo.text"); };
            return t;
        }();
        return &g;
    }

    inline const Shard::Engine::Plugins::EditorUiBackend* Backend()
    {
        static const Shard::Engine::Plugins::EditorUiBackend backend = {
            Widgets(), [](const char*, bool*) { return true; }, [] {},
            [](uint32_t level, const char* text) { Notified().emplace_back(level, text); },
            [] { return Selected(); } };
        return &backend;
    }
}
