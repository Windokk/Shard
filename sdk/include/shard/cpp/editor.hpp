// Shard C++ SDK : windows of the editor (shard_editor.h).
//
//   SHARD_EDITOR_PANEL(Spawner, "Enemy spawner", ui)
//   {
//       static int count = 5;
//       ui.SliderInt("Count", count, 1, 50);
//       if (ui.Button("Spawn")) SpawnEnemies(count);
//   }
//
// The panel appears in the editor's Window menu. In the player, in headless runs and in the offline tools there is no editor : the panel is
// not added and nothing else changes, so the same plugin runs everywhere. A plugin whose manifest hosts include "engine" is loaded by the editor ; "editor" alone limits it to the editor.
#pragma once

#include <cstdio>
#include <exception>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <shard/shard_editor.h>
#include <shard/cpp/component.hpp>
#include <shard/cpp/detail.hpp>
#include <shard/cpp/registry.hpp>

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

    private:
        const ShardUiApi* m_Api;
    };

    using PanelFn = std::function<void(Ui&)>;

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

    private:
        static void SHARD_CALL Trampoline(void* user, const ShardUiApi* api)
        {
            Ui ui(api);
            try {
                (*static_cast<PanelFn*>(user))(ui);
            } catch (const std::exception& e) {
                if (detail::Host()) detail::Host()->log(detail::PluginHandle(), SHARD_LOG_ERROR, Str(std::string("a panel threw : ") + e.what()));
            } catch (...) {
                if (detail::Host()) detail::Host()->log(detail::PluginHandle(), SHARD_LOG_ERROR, Str("a panel threw"));
            }
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
