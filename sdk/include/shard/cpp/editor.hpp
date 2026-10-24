// Shard C++ SDK : windows of the editor (shard_editor.h).
//
//   SHARD_EDITOR_PANEL(Spawner, "Enemy spawner", ui)
//   {
//       static int count = 5;
//       ui.SliderInt("Count", count, 1, 50);
//       if (ui.Button("Spawn")) SpawnEnemies(count);
//   }
//
// The panel appears in the editor's Window menu. Menu items (SHARD_EDITOR_MENU_ITEM), toolbar buttons, inspectors below a component, gizmos in the
// viewport and the selection are in the same header. In the player, in headless runs and in the offline tools there is no editor : the panel is
// not added and nothing else changes, so the same plugin runs everywhere. A plugin whose manifest hosts include "engine" is loaded by the editor ; "editor" alone limits it to the editor.
#pragma once

#include <cstddef>
#include <cstdio>
#include <exception>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <shard/shard_editor.h>
#include <shard/cpp/actor.hpp>
#include <shard/cpp/component.hpp>
#include <shard/cpp/detail.hpp>
#include <shard/cpp/registry.hpp>

// True when the table the host gave has that function (an editor older than this header has a shorter one)
#define SHARD_UI_HAS(name) (m_Api->structSize >= offsetof(ShardUiApi, name) + sizeof(m_Api->name) && m_Api->name != nullptr)

namespace Shard {

    /// The widgets of a panel : valid inside its draw function. Every widget that edits a value takes it by reference and returns true on the
    /// frame the user changed it.
    class Ui {
    public:
        explicit Ui(const ShardUiApi* api) : m_Api(api) {}

        // Text
        void Text(std::string_view text) const { m_Api->text(Str(text)); }
        void TextColored(Color color, std::string_view text) const { const float c[4] = { color.r, color.g, color.b, color.a }; m_Api->text_colored(c, Str(text)); }
        void TextWrapped(std::string_view text) const { m_Api->text_wrapped(Str(text)); }
        void BulletText(std::string_view text) const { m_Api->bullet_text(Str(text)); }
        void LabelText(std::string_view label, std::string_view value) const { m_Api->label_text(Str(label), Str(value)); }

        // Buttons and choices
        bool Button(std::string_view label) const { return m_Api->button(Str(label)) == SHARD_TRUE; }
        bool SmallButton(std::string_view label) const { return m_Api->small_button(Str(label)) == SHARD_TRUE; }
        bool Checkbox(std::string_view label, bool& value) const
        {
            int32_t v = value ? 1 : 0;
            const bool changed = m_Api->checkbox(Str(label), &v) == SHARD_TRUE;
            value = v != 0;
            return changed;
        }
        bool Selectable(std::string_view label, bool selected = false) const { return m_Api->selectable(Str(label), selected ? 1 : 0) == SHARD_TRUE; }
        bool Combo(std::string_view label, int& current, const std::vector<std::string>& items) const
        {
            std::vector<ShardStr> names;
            for (const std::string& s : items) names.push_back(Str(s));
            int32_t c = current;
            const bool changed = m_Api->combo(Str(label), &c, names.data(), uint32_t(names.size())) == SHARD_TRUE;
            current = c;
            return changed;
        }

        // Values
        bool SliderFloat(std::string_view label, float& value, float min, float max) const { return m_Api->slider_float(Str(label), &value, min, max) == SHARD_TRUE; }
        bool SliderInt(std::string_view label, int& value, int min, int max) const
        {
            int32_t v = value;
            const bool changed = m_Api->slider_int(Str(label), &v, min, max) == SHARD_TRUE;
            value = v;
            return changed;
        }
        bool DragFloat(std::string_view label, float& value, float speed = 0.1f, float min = 0.f, float max = 0.f) const { return m_Api->drag_float(Str(label), &value, speed, min, max) == SHARD_TRUE; }
        bool DragInt(std::string_view label, int& value, float speed = 1.f, int min = 0, int max = 0) const
        {
            int32_t v = value;
            const bool changed = m_Api->drag_int(Str(label), &v, speed, min, max) == SHARD_TRUE;
            value = v;
            return changed;
        }
        bool InputFloat(std::string_view label, float& value) const { return m_Api->input_float(Str(label), &value) == SHARD_TRUE; }
        bool InputInt(std::string_view label, int& value) const
        {
            int32_t v = value;
            const bool changed = m_Api->input_int(Str(label), &v) == SHARD_TRUE;
            value = v;
            return changed;
        }
        /// A text box : edits `value` in place (up to 1023 bytes)
        bool InputText(std::string_view label, std::string& value) const
        {
            char buffer[1024];
            std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
            const bool changed = m_Api->input_text(Str(label), buffer, sizeof(buffer)) == SHARD_TRUE;
            if (changed) value = buffer;
            return changed;
        }
        bool ColorEdit(std::string_view label, Color& color) const
        {
            float c[4] = { color.r, color.g, color.b, color.a };
            const bool changed = m_Api->color_edit(Str(label), c) == SHARD_TRUE;
            color = Color{ c[0], c[1], c[2], c[3] };
            return changed;
        }

        // Display
        void ProgressBar(float fraction, std::string_view overlay = {}) const { m_Api->progress_bar(fraction, Str(overlay)); }
        void PlotLines(std::string_view label, const std::vector<float>& values) const { m_Api->plot_lines(Str(label), values.data(), int32_t(values.size())); }

        // Layout
        void Separator() const { m_Api->separator(); }
        void SameLine() const { m_Api->same_line(); }
        void Spacing() const { m_Api->spacing(); }
        void SetNextItemWidth(float width) const { m_Api->set_next_item_width(width); }
        /// True while open : draw the contents only then
        bool CollapsingHeader(std::string_view label) const { return m_Api->collapsing_header(Str(label)) == SHARD_TRUE; }
        /// When it returns true, draw the children and call TreePop
        bool TreeNode(std::string_view label) const { return m_Api->tree_node(Str(label)) == SHARD_TRUE; }
        void TreePop() const { m_Api->tree_pop(); }
        void PushId(std::string_view id) const { m_Api->push_id(Str(id)); }
        void PopId() const { m_Api->pop_id(); }

        // The widget drawn last
        bool IsItemHovered() const { return m_Api->is_item_hovered() == SHARD_TRUE; }
        void Tooltip(std::string_view text) const { m_Api->tooltip(Str(text)); }

        // ---- shard.editor 1.1 : an older editor does not have these (the call does nothing and returns false) ----

        void TextDisabled(std::string_view text) const { if (SHARD_UI_HAS(text_disabled)) m_Api->text_disabled(Str(text)); }
        void SeparatorText(std::string_view label) const { if (SHARD_UI_HAS(separator_text)) m_Api->separator_text(Str(label)); }
        /// On when `value == buttonValue`, sets it so when clicked
        bool RadioButton(std::string_view label, int& value, int buttonValue) const
        {
            if (!SHARD_UI_HAS(radio_button)) return false;
            int32_t v = value;
            const bool changed = m_Api->radio_button(Str(label), &v, buttonValue) == SHARD_TRUE;
            value = v;
            return changed;
        }
        bool DragVec3(std::string_view label, Vec3& value, float speed = 0.1f, float min = 0.f, float max = 0.f) const
        {
            if (!SHARD_UI_HAS(drag_float3)) return false;
            float v[3] = { value.x, value.y, value.z };
            const bool changed = m_Api->drag_float3(Str(label), v, speed, min, max) == SHARD_TRUE;
            value = Vec3{ v[0], v[1], v[2] };
            return changed;
        }
        bool DragVec2(std::string_view label, Vec2& value, float speed = 0.1f, float min = 0.f, float max = 0.f) const
        {
            if (!SHARD_UI_HAS(drag_float2)) return false;
            float v[2] = { value.x, value.y };
            const bool changed = m_Api->drag_float2(Str(label), v, speed, min, max) == SHARD_TRUE;
            value = Vec2{ v[0], v[1] };
            return changed;
        }
        bool ColorEdit3(std::string_view label, Color& color) const
        {
            if (!SHARD_UI_HAS(color_edit3)) return false;
            float c[3] = { color.r, color.g, color.b };
            const bool changed = m_Api->color_edit3(Str(label), c) == SHARD_TRUE;
            color = Color{ c[0], c[1], c[2], color.a };
            return changed;
        }
        /// A text box of several lines : edits `value` in place (up to 4095 bytes)
        bool InputTextMultiline(std::string_view label, std::string& value, float width = 0.f, float height = 0.f) const
        {
            if (!SHARD_UI_HAS(input_text_multiline)) return false;
            char buffer[4096];
            std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
            const bool changed = m_Api->input_text_multiline(Str(label), buffer, sizeof(buffer), width, height) == SHARD_TRUE;
            if (changed) value = buffer;
            return changed;
        }
        bool Button(std::string_view label, float width, float height) const { return SHARD_UI_HAS(button_size) && m_Api->button_size(Str(label), width, height) == SHARD_TRUE; }
        void PlotHistogram(std::string_view label, const std::vector<float>& values) const { if (SHARD_UI_HAS(plot_histogram)) m_Api->plot_histogram(Str(label), values.data(), int32_t(values.size())); }

        // Layout
        void BeginGroup() const { if (SHARD_UI_HAS(begin_group)) m_Api->begin_group(); }
        void EndGroup() const { if (SHARD_UI_HAS(end_group)) m_Api->end_group(); }
        void Indent(float width = 0.f) const { if (SHARD_UI_HAS(indent)) m_Api->indent(width); }
        void Unindent(float width = 0.f) const { if (SHARD_UI_HAS(unindent)) m_Api->unindent(width); }
        void Dummy(float width, float height) const { if (SHARD_UI_HAS(dummy)) m_Api->dummy(width, height); }
        void NewLine() const { if (SHARD_UI_HAS(new_line)) m_Api->new_line(); }
        void PushItemWidth(float width) const { if (SHARD_UI_HAS(push_item_width)) m_Api->push_item_width(width); }
        void PopItemWidth() const { if (SHARD_UI_HAS(pop_item_width)) m_Api->pop_item_width(); }
        float ContentWidth() const { return SHARD_UI_HAS(content_width) ? m_Api->content_width() : 0.f; }
        /// A region that scrolls on its own : always call EndChild
        bool BeginChild(std::string_view id, float width = 0.f, float height = 0.f, bool border = false) const { return SHARD_UI_HAS(begin_child) && m_Api->begin_child(Str(id), width, height, border ? 1 : 0) == SHARD_TRUE; }
        void EndChild() const { if (SHARD_UI_HAS(end_child)) m_Api->end_child(); }
        void BeginDisabled(bool disabled = true) const { if (SHARD_UI_HAS(begin_disabled)) m_Api->begin_disabled(disabled ? 1 : 0); }
        void EndDisabled() const { if (SHARD_UI_HAS(end_disabled)) m_Api->end_disabled(); }
        void PushTextColor(Color color) const { const float c[4] = { color.r, color.g, color.b, color.a }; if (SHARD_UI_HAS(push_text_color)) m_Api->push_text_color(c); }
        void PopTextColor() const { if (SHARD_UI_HAS(pop_text_color)) m_Api->pop_text_color(); }

        // Tabs : if (ui.BeginTabBar("tabs")) { if (ui.BeginTabItem("A")) { ...; ui.EndTabItem(); } ui.EndTabBar(); }
        bool BeginTabBar(std::string_view id) const { return SHARD_UI_HAS(begin_tab_bar) && m_Api->begin_tab_bar(Str(id)) == SHARD_TRUE; }
        void EndTabBar() const { if (SHARD_UI_HAS(end_tab_bar)) m_Api->end_tab_bar(); }
        bool BeginTabItem(std::string_view label) const { return SHARD_UI_HAS(begin_tab_item) && m_Api->begin_tab_item(Str(label)) == SHARD_TRUE; }
        void EndTabItem() const { if (SHARD_UI_HAS(end_tab_item)) m_Api->end_tab_item(); }

        // Tables : if (ui.BeginTable("t", 2)) { ui.TableSetupColumn("Name"); ...; ui.TableHeadersRow(); ui.TableNextRow(); ui.TableNextColumn(); ...; ui.EndTable(); }
        bool BeginTable(std::string_view id, int columns) const { return SHARD_UI_HAS(begin_table) && m_Api->begin_table(Str(id), columns) == SHARD_TRUE; }
        void EndTable() const { if (SHARD_UI_HAS(end_table)) m_Api->end_table(); }
        void TableSetupColumn(std::string_view label) const { if (SHARD_UI_HAS(table_setup_column)) m_Api->table_setup_column(Str(label)); }
        void TableHeadersRow() const { if (SHARD_UI_HAS(table_headers_row)) m_Api->table_headers_row(); }
        void TableNextRow() const { if (SHARD_UI_HAS(table_next_row)) m_Api->table_next_row(); }
        bool TableNextColumn() const { return SHARD_UI_HAS(table_next_column) && m_Api->table_next_column() == SHARD_TRUE; }

        // Menus and popups (end only what returned true)
        bool BeginMenu(std::string_view label) const { return SHARD_UI_HAS(begin_menu) && m_Api->begin_menu(Str(label)) == SHARD_TRUE; }
        void EndMenu() const { if (SHARD_UI_HAS(end_menu)) m_Api->end_menu(); }
        bool MenuItem(std::string_view label, std::string_view shortcut = {}, bool selected = false, bool enabled = true) const
        {
            return SHARD_UI_HAS(menu_item) && m_Api->menu_item(Str(label), Str(shortcut), selected ? 1 : 0, enabled ? 1 : 0) == SHARD_TRUE;
        }
        void OpenPopup(std::string_view id) const { if (SHARD_UI_HAS(open_popup)) m_Api->open_popup(Str(id)); }
        bool BeginPopup(std::string_view id) const { return SHARD_UI_HAS(begin_popup) && m_Api->begin_popup(Str(id)) == SHARD_TRUE; }
        void EndPopup() const { if (SHARD_UI_HAS(end_popup)) m_Api->end_popup(); }
        void CloseCurrentPopup() const { if (SHARD_UI_HAS(close_current_popup)) m_Api->close_current_popup(); }
        /// A popup opened by a right click on the widget drawn last
        bool BeginPopupContextItem() const { return SHARD_UI_HAS(begin_popup_context_item) && m_Api->begin_popup_context_item() == SHARD_TRUE; }

        // The widget drawn last (button : 0 left, 1 right)
        bool IsItemClicked(int button = 0) const { return SHARD_UI_HAS(is_item_clicked) && m_Api->is_item_clicked(button) == SHARD_TRUE; }
        bool IsItemActive() const { return SHARD_UI_HAS(is_item_active) && m_Api->is_item_active() == SHARD_TRUE; }
        bool IsItemDoubleClicked() const { return SHARD_UI_HAS(is_item_double_clicked) && m_Api->is_item_double_clicked() == SHARD_TRUE; }

    private:
        const ShardUiApi* m_Api;
    };

#undef SHARD_UI_HAS

    /// What a gizmo draws with, in the world : valid inside its draw function
    class Gizmo {
    public:
        explicit Gizmo(const ShardGizmoApi* api) : m_Api(api) {}
        void Line(Vec3 from, Vec3 to, Color color = {}) const { const float a[3] = { from.x, from.y, from.z }, b[3] = { to.x, to.y, to.z }, c[4] = { color.r, color.g, color.b, color.a }; m_Api->line(a, b, c); }
        void Point(Vec3 position, float size = 3.f, Color color = {}) const { const float p[3] = { position.x, position.y, position.z }, c[4] = { color.r, color.g, color.b, color.a }; m_Api->point(p, size, c); }
        /// A circle around `center`, in the plane whose normal is `normal`
        void Circle(Vec3 center, Vec3 normal, float radius, Color color = {}) const
        {
            const float p[3] = { center.x, center.y, center.z }, n[3] = { normal.x, normal.y, normal.z }, c[4] = { color.r, color.g, color.b, color.a };
            m_Api->circle(p, n, radius, c);
        }
        void Sphere(Vec3 center, float radius, Color color = {}) const { const float p[3] = { center.x, center.y, center.z }, c[4] = { color.r, color.g, color.b, color.a }; m_Api->sphere(p, radius, c); }
        void Box(Vec3 center, Vec3 halfExtent, Color color = {}) const
        {
            const float p[3] = { center.x, center.y, center.z }, h[3] = { halfExtent.x, halfExtent.y, halfExtent.z }, c[4] = { color.r, color.g, color.b, color.a };
            m_Api->box(p, h, c);
        }
        void Arrow(Vec3 from, Vec3 to, Color color = {}) const { const float a[3] = { from.x, from.y, from.z }, b[3] = { to.x, to.y, to.z }, c[4] = { color.r, color.g, color.b, color.a }; m_Api->arrow(a, b, c); }
        void Text(Vec3 position, std::string_view text, Color color = {}) const { const float p[3] = { position.x, position.y, position.z }, c[4] = { color.r, color.g, color.b, color.a }; m_Api->text(p, Str(text), c); }

    private:
        const ShardGizmoApi* m_Api;
    };

    using PanelFn = std::function<void(Ui&)>;
    using ActionFn = std::function<void()>;
    using InspectorFn = std::function<void(Ui&, Actor)>;
    using GizmoFn = std::function<void(Gizmo&, Actor)>;
    using SelectionFn = std::function<void(const std::vector<Actor>&)>;

    class Editor {
    public:
        explicit Editor(ShardPluginCtx ctx) : m_Ctx(ctx) {}

        /// True when the host is the editor
        static bool Available() { return detail::Interface<ShardEditorApi>(SHARD_IFACE_EDITOR) != nullptr; }

        /// Adds a panel named `title` to the Window menu. Returns false outside the editor (nothing is added) or when this plugin already has a
        /// panel of that name. `draw` runs every frame while the window is open.
        bool AddPanel(const std::string& title, PanelFn draw)
        {
            const ShardEditorApi* api = detail::Interface<ShardEditorApi>(SHARD_IFACE_EDITOR);
            if (!api) return false;
            PanelFn* kept = detail::Keep(std::move(draw));
            return api->add_panel(m_Ctx, Str(title), &Trampoline, kept) == SHARD_OK;
        }

        /// An item of a menu of the menu bar: `path` is "Menu/Item" or "Menu/Submenu/Item" ("Tools/Bake/All levels"), `shortcut` is only displayed.
        /// False outside the editor, in an editor older than shard.editor 1.1, or when this plugin already has an item there.
        bool AddMenuItem(const std::string& path, ActionFn run, const std::string& shortcut = {})
        {
            const ShardEditorApi* api = Api11();
            if (!api || !api->add_menu_item) return false;
            ActionFn* kept = detail::Keep(std::move(run));
            return api->add_menu_item(m_Ctx, Str(path), Str(shortcut), &Action, kept) == SHARD_OK;
        }

        /// A button in the viewport's toolbar
        bool AddToolbarButton(const std::string& label, ActionFn run, const std::string& tooltip = {})
        {
            const ShardEditorApi* api = Api11();
            if (!api || !api->add_toolbar_button) return false;
            ActionFn* kept = detail::Keep(std::move(run));
            return api->add_toolbar_button(m_Ctx, Str(label), Str(tooltip), &Action, kept) == SHARD_OK;
        }

        /// Draws below the component `componentType` of the actor the Properties panel shows ("light", "my.plugin.Health"), or below all of them (empty)
        bool AddInspector(const std::string& componentType, InspectorFn draw)
        {
            const ShardEditorApi* api = Api11();
            if (!api || !api->add_inspector) return false;
            InspectorFn* kept = detail::Keep(std::move(draw));
            return api->add_inspector(m_Ctx, Str(componentType), &Inspector, kept) == SHARD_OK;
        }

        /// Draws in the viewport for every selected actor
        bool AddGizmo(const std::string& name, GizmoFn draw)
        {
            const ShardEditorApi* api = Api11();
            if (!api || !api->add_gizmo) return false;
            GizmoFn* kept = detail::Keep(std::move(draw));
            return api->add_gizmo(m_Ctx, Str(name), &GizmoCall, kept) == SHARD_OK;
        }

        /// Called when the selection changes: the selected actors, the primary one last
        bool OnSelectionChanged(SelectionFn fn)
        {
            const ShardEditorApi* api = Api11();
            if (!api || !api->subscribe_selection) return false;
            SelectionFn* kept = detail::Keep(std::move(fn));
            return api->subscribe_selection(m_Ctx, &Selection, kept) == SHARD_OK;
        }

        /// The selected actors, the primary one last
        static std::vector<Actor> SelectedActors()
        {
            std::vector<Actor> out;
            const ShardEditorApi* api = Api11();
            if (!api || !api->get_selection) return out;
            uint32_t count = 0;
            if (api->get_selection(nullptr, 0, &count) != SHARD_OK || count == 0) return out;
            std::vector<ShardActor> handles(count);
            if (api->get_selection(handles.data(), count, &count) != SHARD_OK) return out;
            for (uint32_t i = 0; i < count && i < handles.size(); ++i) out.emplace_back(handles[i]);
            return out;
        }

        /// A notification in the corner of the editor
        void Notify(std::string_view message, uint32_t level = SHARD_EDITOR_NOTIFY_INFO) const
        {
            const ShardEditorApi* api = Api11();
            if (api && api->notify) api->notify(m_Ctx, level, Str(message));
        }

        /// Shows or hides one of this plugin's panels
        bool SetPanelVisible(const std::string& title, bool visible) const
        {
            const ShardEditorApi* api = Api11();
            return api && api->set_panel_visible && api->set_panel_visible(m_Ctx, Str(title), visible ? 1 : 0) == SHARD_OK;
        }

    private:
        static const ShardEditorApi* Api11() { return detail::Interface<ShardEditorApi>(SHARD_IFACE_EDITOR, 1); }

        // What the callbacks catch is the plugin's: an exception of a function of the editor is logged and does not cross the ABI
        template <typename F>
        static void Guarded(const char* what, F&& f)
        {
#if SHARD_SDK_NOEXC
            (void)what;
            f();
#else
            try {
                f();
            } catch (const std::exception& e) {
                if (detail::Host()) detail::Host()->log(detail::PluginHandle(), SHARD_LOG_ERROR, Str(std::string(what) + " threw : " + e.what()));
            } catch (...) {
                if (detail::Host()) detail::Host()->log(detail::PluginHandle(), SHARD_LOG_ERROR, Str(std::string(what) + " threw"));
            }
#endif
        }
        static void SHARD_CALL Trampoline(void* user, const ShardUiApi* api)
        {
            Ui ui(api);
            Guarded("a panel", [&] { (*static_cast<PanelFn*>(user))(ui); });
        }
        static void SHARD_CALL Action(void* user) { Guarded("a menu item or a toolbar button", [&] { (*static_cast<ActionFn*>(user))(); }); }
        static void SHARD_CALL Inspector(void* user, const ShardUiApi* api, ShardActor actor)
        {
            Ui ui(api);
            Guarded("an inspector", [&] { (*static_cast<InspectorFn*>(user))(ui, Actor(actor)); });
        }
        static void SHARD_CALL GizmoCall(void* user, const ShardGizmoApi* api, ShardActor actor)
        {
            Gizmo gizmo(api);
            Guarded("a gizmo", [&] { (*static_cast<GizmoFn*>(user))(gizmo, Actor(actor)); });
        }
        static void SHARD_CALL Selection(void* user, const ShardActor* actors, uint32_t count)
        {
            std::vector<Actor> list;
            for (uint32_t i = 0; i < count; ++i) list.emplace_back(actors[i]);
            Guarded("a selection handler", [&] { (*static_cast<SelectionFn*>(user))(list); });
        }

        ShardPluginCtx m_Ctx;
    };
}

/// A panel of the editor, defined and registered from any file. `ui` is the name of the Shard::Ui the body draws with. At global scope.
#define SHARD_EDITOR_PANEL(Name, Title, ui)                                                                         \
    static void SHARD_DETAIL_REG_CAT(ShardPanel_, Name)(::Shard::Ui& ui);                                           \
    static ::Shard::detail::Registrar SHARD_DETAIL_REG_CAT(shardPanelRegistrar_, Name)(                             \
        "4panel:" #Name, [](::Shard::Context& ctx) { ctx.Editor().AddPanel(Title, &SHARD_DETAIL_REG_CAT(ShardPanel_, Name)); }); \
    static void SHARD_DETAIL_REG_CAT(ShardPanel_, Name)(::Shard::Ui& ui)

/// An item of a menu of the menu bar ("Tools/My tool"), defined and registered from any file. The body runs when it is clicked. At global scope.
#define SHARD_EDITOR_MENU_ITEM(Name, Path)                                                                          \
    static void SHARD_DETAIL_REG_CAT(ShardMenu_, Name)();                                                           \
    static ::Shard::detail::Registrar SHARD_DETAIL_REG_CAT(shardMenuRegistrar_, Name)(                              \
        "4menu:" #Name, [](::Shard::Context& ctx) { ctx.Editor().AddMenuItem(Path, &SHARD_DETAIL_REG_CAT(ShardMenu_, Name)); }); \
    static void SHARD_DETAIL_REG_CAT(ShardMenu_, Name)()

/// A button of the viewport's toolbar, defined and registered from any file. At global scope.
#define SHARD_EDITOR_TOOLBAR_BUTTON(Name, Label)                                                                    \
    static void SHARD_DETAIL_REG_CAT(ShardToolbar_, Name)();                                                        \
    static ::Shard::detail::Registrar SHARD_DETAIL_REG_CAT(shardToolbarRegistrar_, Name)(                           \
        "4toolbar:" #Name, [](::Shard::Context& ctx) { ctx.Editor().AddToolbarButton(Label, &SHARD_DETAIL_REG_CAT(ShardToolbar_, Name)); }); \
    static void SHARD_DETAIL_REG_CAT(ShardToolbar_, Name)()

/// What is drawn below the component `ComponentType` ("" : all of them) in the Properties panel. `ui` and `actor` are the names the body uses. At global scope.
#define SHARD_EDITOR_INSPECTOR(Name, ComponentType, ui, actor)                                                      \
    static void SHARD_DETAIL_REG_CAT(ShardInspector_, Name)(::Shard::Ui& ui, ::Shard::Actor actor);                 \
    static ::Shard::detail::Registrar SHARD_DETAIL_REG_CAT(shardInspectorRegistrar_, Name)(                         \
        "4inspector:" #Name, [](::Shard::Context& ctx) { ctx.Editor().AddInspector(ComponentType, &SHARD_DETAIL_REG_CAT(ShardInspector_, Name)); }); \
    static void SHARD_DETAIL_REG_CAT(ShardInspector_, Name)(::Shard::Ui& ui, ::Shard::Actor actor)

/// A gizmo drawn in the viewport for each selected actor. At global scope.
#define SHARD_EDITOR_GIZMO(Name, gizmo, actor)                                                                      \
    static void SHARD_DETAIL_REG_CAT(ShardGizmo_, Name)(::Shard::Gizmo& gizmo, ::Shard::Actor actor);               \
    static ::Shard::detail::Registrar SHARD_DETAIL_REG_CAT(shardGizmoRegistrar_, Name)(                             \
        "4gizmo:" #Name, [](::Shard::Context& ctx) { ctx.Editor().AddGizmo(#Name, &SHARD_DETAIL_REG_CAT(ShardGizmo_, Name)); }); \
    static void SHARD_DETAIL_REG_CAT(ShardGizmo_, Name)(::Shard::Gizmo& gizmo, ::Shard::Actor actor)
