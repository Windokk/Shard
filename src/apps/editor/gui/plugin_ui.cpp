#include "plugin_ui.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "imgui/imgui.h"

#include <shard/shard_editor.h>
#include <shard/shard_input.h>

#include "apps/editor/commands/command_stack.hpp"
#include "apps/editor/gui/main_window.hpp"
#include "apps/editor/gui/notifications.hpp"
#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/plugins/manager.hpp"
#include "engine/world/actor.hpp"

namespace Shard::Editor::GUI {

    namespace {
        // A ShardStr is not zero terminated and ImGui wants one. The strings are short and used one at a time, so a few buffers rotate.
        const char* Z(ShardStr s)
        {
            static thread_local std::string buffers[8];
            static thread_local unsigned next = 0;
            std::string& b = buffers[next++ % 8];
            b.assign(s.data ? s.data : "", s.data ? size_t(s.size) : 0);
            return b.c_str();
        }

        void SHARD_CALL Text(ShardStr t) { ImGui::TextUnformatted(t.data ? t.data : "", t.data ? t.data + t.size : nullptr); }
        void SHARD_CALL TextColored(const float rgba[4], ShardStr t) { ImGui::TextColored(ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]), "%s", Z(t)); }
        void SHARD_CALL TextWrapped(ShardStr t) { ImGui::TextWrapped("%s", Z(t)); }
        void SHARD_CALL BulletText(ShardStr t) { ImGui::BulletText("%s", Z(t)); }
        void SHARD_CALL LabelText(ShardStr label, ShardStr value)
        {
            const std::string l = Z(label);
            ImGui::LabelText(l.c_str(), "%s", Z(value));
        }

        int32_t SHARD_CALL Button(ShardStr l) { return ImGui::Button(Z(l)) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL SmallButton(ShardStr l) { return ImGui::SmallButton(Z(l)) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL Checkbox(ShardStr l, int32_t* value)
        {
            if (!value) return SHARD_FALSE;
            bool b = *value != 0;
            const bool changed = ImGui::Checkbox(Z(l), &b);
            *value = b ? 1 : 0;
            return changed ? SHARD_TRUE : SHARD_FALSE;
        }
        int32_t SHARD_CALL Selectable(ShardStr l, int32_t selected) { return ImGui::Selectable(Z(l), selected != 0) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL Combo(ShardStr l, int32_t* current, const ShardStr* items, uint32_t count)
        {
            if (!current || (count && !items)) return SHARD_FALSE;
            std::vector<std::string> names(count);
            for (uint32_t i = 0; i < count; ++i) names[i].assign(items[i].data ? items[i].data : "", items[i].data ? size_t(items[i].size) : 0);
            bool changed = false;
            const char* preview = (*current >= 0 && uint32_t(*current) < count) ? names[size_t(*current)].c_str() : "";
            if (ImGui::BeginCombo(Z(l), preview)) {
                for (uint32_t i = 0; i < count; ++i) {
                    const bool selected = int32_t(i) == *current;
                    if (ImGui::Selectable(names[i].c_str(), selected)) { *current = int32_t(i); changed = true; }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            return changed ? SHARD_TRUE : SHARD_FALSE;
        }

        int32_t SHARD_CALL SliderFloat(ShardStr l, float* v, float lo, float hi) { return v && ImGui::SliderFloat(Z(l), v, lo, hi) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL SliderInt(ShardStr l, int32_t* v, int32_t lo, int32_t hi) { return v && ImGui::SliderInt(Z(l), reinterpret_cast<int*>(v), lo, hi) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL DragFloat(ShardStr l, float* v, float speed, float lo, float hi) { return v && ImGui::DragFloat(Z(l), v, speed, lo, hi) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL DragInt(ShardStr l, int32_t* v, float speed, int32_t lo, int32_t hi) { return v && ImGui::DragInt(Z(l), reinterpret_cast<int*>(v), speed, lo, hi) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL InputFloat(ShardStr l, float* v) { return v && ImGui::InputFloat(Z(l), v) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL InputInt(ShardStr l, int32_t* v) { return v && ImGui::InputInt(Z(l), reinterpret_cast<int*>(v)) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL InputText(ShardStr l, char* buffer, uint64_t capacity)
        {
            if (!buffer || capacity == 0) return SHARD_FALSE;
            buffer[capacity - 1] = '\0';
            return ImGui::InputText(Z(l), buffer, size_t(capacity)) ? SHARD_TRUE : SHARD_FALSE;
        }
        int32_t SHARD_CALL ColorEdit(ShardStr l, float rgba[4]) { return rgba && ImGui::ColorEdit4(Z(l), rgba) ? SHARD_TRUE : SHARD_FALSE; }

        void SHARD_CALL ProgressBar(float fraction, ShardStr overlay) { ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay.size ? Z(overlay) : nullptr); }
        void SHARD_CALL PlotLines(ShardStr l, const float* values, int32_t count) { if (values && count > 0) ImGui::PlotLines(Z(l), values, count); }

        void SHARD_CALL Separator() { ImGui::Separator(); }
        void SHARD_CALL SameLine() { ImGui::SameLine(); }
        void SHARD_CALL Spacing() { ImGui::Spacing(); }
        void SHARD_CALL SetNextItemWidth(float w) { ImGui::SetNextItemWidth(w); }
        int32_t SHARD_CALL CollapsingHeader(ShardStr l) { return ImGui::CollapsingHeader(Z(l)) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL TreeNode(ShardStr l) { return ImGui::TreeNode(Z(l)) ? SHARD_TRUE : SHARD_FALSE; }
        void SHARD_CALL TreePop() { ImGui::TreePop(); }
        void SHARD_CALL PushId(ShardStr id) { ImGui::PushID(Z(id)); }
        void SHARD_CALL PopId() { ImGui::PopID(); }

        int32_t SHARD_CALL IsItemHovered() { return ImGui::IsItemHovered() ? SHARD_TRUE : SHARD_FALSE; }
        void SHARD_CALL Tooltip(ShardStr t) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Z(t)); }

        // ---- 1.1 ----

        void SHARD_CALL TextDisabled(ShardStr t) { ImGui::TextDisabled("%s", Z(t)); }
        void SHARD_CALL SeparatorText(ShardStr t) { ImGui::SeparatorText(Z(t)); }
        int32_t SHARD_CALL RadioButton(ShardStr l, int32_t* value, int32_t buttonValue) { return value && ImGui::RadioButton(Z(l), reinterpret_cast<int*>(value), buttonValue) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL DragFloat2(ShardStr l, float* v, float speed, float lo, float hi) { return v && ImGui::DragFloat2(Z(l), v, speed, lo, hi) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL DragFloat3(ShardStr l, float* v, float speed, float lo, float hi) { return v && ImGui::DragFloat3(Z(l), v, speed, lo, hi) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL DragFloat4(ShardStr l, float* v, float speed, float lo, float hi) { return v && ImGui::DragFloat4(Z(l), v, speed, lo, hi) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL ColorEdit3(ShardStr l, float rgb[3]) { return rgb && ImGui::ColorEdit3(Z(l), rgb) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL InputTextMultiline(ShardStr l, char* buffer, uint64_t capacity, float width, float height)
        {
            if (!buffer || capacity == 0) return SHARD_FALSE;
            buffer[capacity - 1] = '\0';
            return ImGui::InputTextMultiline(Z(l), buffer, size_t(capacity), ImVec2(width, height)) ? SHARD_TRUE : SHARD_FALSE;
        }
        int32_t SHARD_CALL ButtonSize(ShardStr l, float w, float h) { return ImGui::Button(Z(l), ImVec2(w, h)) ? SHARD_TRUE : SHARD_FALSE; }
        void SHARD_CALL PlotHistogram(ShardStr l, const float* values, int32_t count) { if (values && count > 0) ImGui::PlotHistogram(Z(l), values, count); }

        void SHARD_CALL BeginGroup() { ImGui::BeginGroup(); }
        void SHARD_CALL EndGroup() { ImGui::EndGroup(); }
        void SHARD_CALL Indent(float w) { ImGui::Indent(w); }
        void SHARD_CALL Unindent(float w) { ImGui::Unindent(w); }
        void SHARD_CALL Dummy(float w, float h) { ImGui::Dummy(ImVec2(w, h)); }
        void SHARD_CALL NewLine() { ImGui::NewLine(); }
        void SHARD_CALL PushItemWidth(float w) { ImGui::PushItemWidth(w); }
        void SHARD_CALL PopItemWidth() { ImGui::PopItemWidth(); }
        int32_t SHARD_CALL BeginChild(ShardStr id, float w, float h, int32_t border) { return ImGui::BeginChild(Z(id), ImVec2(w, h), border ? ImGuiChildFlags_Borders : 0) ? SHARD_TRUE : SHARD_FALSE; }
        void SHARD_CALL EndChild() { ImGui::EndChild(); }
        void SHARD_CALL BeginDisabled(int32_t disabled) { ImGui::BeginDisabled(disabled != 0); }
        void SHARD_CALL EndDisabled() { ImGui::EndDisabled(); }
        void SHARD_CALL PushTextColor(const float rgba[4]) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(rgba[0], rgba[1], rgba[2], rgba[3])); }
        void SHARD_CALL PopTextColor() { ImGui::PopStyleColor(); }
        float SHARD_CALL ContentWidth() { return ImGui::GetContentRegionAvail().x; }

        int32_t SHARD_CALL BeginTabBar(ShardStr id) { return ImGui::BeginTabBar(Z(id)) ? SHARD_TRUE : SHARD_FALSE; }
        void SHARD_CALL EndTabBar() { ImGui::EndTabBar(); }
        int32_t SHARD_CALL BeginTabItem(ShardStr l) { return ImGui::BeginTabItem(Z(l)) ? SHARD_TRUE : SHARD_FALSE; }
        void SHARD_CALL EndTabItem() { ImGui::EndTabItem(); }

        int32_t SHARD_CALL BeginTable(ShardStr id, int32_t columns) { return columns > 0 && ImGui::BeginTable(Z(id), columns, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable) ? SHARD_TRUE : SHARD_FALSE; }
        void SHARD_CALL EndTable() { ImGui::EndTable(); }
        void SHARD_CALL TableSetupColumn(ShardStr l) { ImGui::TableSetupColumn(Z(l)); }
        void SHARD_CALL TableHeadersRow() { ImGui::TableHeadersRow(); }
        void SHARD_CALL TableNextRow() { ImGui::TableNextRow(); }
        int32_t SHARD_CALL TableNextColumn() { return ImGui::TableNextColumn() ? SHARD_TRUE : SHARD_FALSE; }

        int32_t SHARD_CALL BeginMenu(ShardStr l) { return ImGui::BeginMenu(Z(l)) ? SHARD_TRUE : SHARD_FALSE; }
        void SHARD_CALL EndMenu() { ImGui::EndMenu(); }
        int32_t SHARD_CALL MenuItem(ShardStr l, ShardStr shortcut, int32_t selected, int32_t enabled)
        {
            const std::string label = Z(l);
            return ImGui::MenuItem(label.c_str(), shortcut.size ? Z(shortcut) : nullptr, selected != 0, enabled != 0) ? SHARD_TRUE : SHARD_FALSE;
        }
        void SHARD_CALL OpenPopup(ShardStr id) { ImGui::OpenPopup(Z(id)); }
        int32_t SHARD_CALL BeginPopup(ShardStr id) { return ImGui::BeginPopup(Z(id)) ? SHARD_TRUE : SHARD_FALSE; }
        void SHARD_CALL EndPopup() { ImGui::EndPopup(); }
        void SHARD_CALL CloseCurrentPopup() { ImGui::CloseCurrentPopup(); }
        int32_t SHARD_CALL BeginPopupContextItem() { return ImGui::BeginPopupContextItem() ? SHARD_TRUE : SHARD_FALSE; }

        int32_t SHARD_CALL IsItemClicked(int32_t button) { return ImGui::IsItemClicked(button == 1 ? ImGuiMouseButton_Right : ImGuiMouseButton_Left) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL IsItemActive() { return ImGui::IsItemActive() ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL IsItemDoubleClicked() { return ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) ? SHARD_TRUE : SHARD_FALSE; }

        // ---- 1.2 : a draw list, the mouse and keyboard, hit areas, images, drag and drop ----

        ImVec2 V2(const float v[2]) { return ImVec2(v[0], v[1]); }

        void SHARD_CALL DrawLine(const float a[2], const float b[2], uint32_t color, float thickness) { ImGui::GetWindowDrawList()->AddLine(V2(a), V2(b), color, thickness); }
        void SHARD_CALL DrawPolyline(const float* points, uint32_t count, uint32_t color, int32_t closed, float thickness)
        {
            if (!points || count < 2) return;
            ImGui::GetWindowDrawList()->AddPolyline(reinterpret_cast<const ImVec2*>(points), int(count), color, closed ? ImDrawFlags_Closed : ImDrawFlags_None, thickness);
        }
        void SHARD_CALL DrawRect(const float lo[2], const float hi[2], uint32_t color, float rounding, float thickness) { ImGui::GetWindowDrawList()->AddRect(V2(lo), V2(hi), color, rounding, 0, thickness); }
        void SHARD_CALL DrawRectFilled(const float lo[2], const float hi[2], uint32_t color, float rounding) { ImGui::GetWindowDrawList()->AddRectFilled(V2(lo), V2(hi), color, rounding); }
        void SHARD_CALL DrawCircle(const float c[2], float radius, uint32_t color, float thickness) { ImGui::GetWindowDrawList()->AddCircle(V2(c), radius, color, 0, thickness); }
        void SHARD_CALL DrawCircleFilled(const float c[2], float radius, uint32_t color) { ImGui::GetWindowDrawList()->AddCircleFilled(V2(c), radius, color); }
        void SHARD_CALL DrawTriangleFilled(const float a[2], const float b[2], const float c[2], uint32_t color) { ImGui::GetWindowDrawList()->AddTriangleFilled(V2(a), V2(b), V2(c), color); }
        void SHARD_CALL DrawBezier(const float p0[2], const float p1[2], const float p2[2], const float p3[2], uint32_t color, float thickness) { ImGui::GetWindowDrawList()->AddBezierCubic(V2(p0), V2(p1), V2(p2), V2(p3), color, thickness); }
        void SHARD_CALL DrawText(const float position[2], uint32_t color, ShardStr text)
        {
            ImGui::GetWindowDrawList()->AddText(V2(position), color, text.data ? text.data : "", text.data ? text.data + text.size : nullptr);
        }
        void SHARD_CALL PushClipRect(const float lo[2], const float hi[2], int32_t intersect) { ImGui::GetWindowDrawList()->PushClipRect(V2(lo), V2(hi), intersect != 0); }
        void SHARD_CALL PopClipRect() { ImGui::GetWindowDrawList()->PopClipRect(); }

        void SHARD_CALL GetCursorScreenPos(float out[2]) { const ImVec2 p = ImGui::GetCursorScreenPos(); out[0] = p.x; out[1] = p.y; }
        void SHARD_CALL SetCursorScreenPos(const float position[2]) { ImGui::SetCursorScreenPos(V2(position)); }
        void SHARD_CALL GetContentRegionAvail(float out[2]) { const ImVec2 p = ImGui::GetContentRegionAvail(); out[0] = p.x; out[1] = p.y; }
        void SHARD_CALL GetItemRectMin(float out[2]) { const ImVec2 p = ImGui::GetItemRectMin(); out[0] = p.x; out[1] = p.y; }
        void SHARD_CALL GetItemRectMax(float out[2]) { const ImVec2 p = ImGui::GetItemRectMax(); out[0] = p.x; out[1] = p.y; }
        void SHARD_CALL CalcTextSize(ShardStr text, float out[2])
        {
            const ImVec2 p = ImGui::CalcTextSize(text.data ? text.data : "", text.data ? text.data + text.size : nullptr);
            out[0] = p.x; out[1] = p.y;
        }
        int32_t SHARD_CALL InvisibleButton(ShardStr id, float w, float h) { return ImGui::InvisibleButton(Z(id), ImVec2(w, h)) ? SHARD_TRUE : SHARD_FALSE; }

        ImGuiMouseButton Button(int32_t button) { return button == SHARD_MOUSE_RIGHT ? ImGuiMouseButton_Right : button == SHARD_MOUSE_MIDDLE ? ImGuiMouseButton_Middle : ImGuiMouseButton_Left; }

        void SHARD_CALL GetMousePos(float out[2]) { const ImVec2 p = ImGui::GetMousePos(); out[0] = p.x; out[1] = p.y; }
        void SHARD_CALL GetMouseDragDelta(int32_t button, float out[2]) { const ImVec2 p = ImGui::GetMouseDragDelta(Button(button), 0.0f); out[0] = p.x; out[1] = p.y; }
        void SHARD_CALL ResetMouseDragDelta(int32_t button) { ImGui::ResetMouseDragDelta(Button(button)); }
        float SHARD_CALL GetMouseWheel() { return ImGui::GetIO().MouseWheel; }
        int32_t SHARD_CALL IsMouseDown(int32_t button) { return ImGui::IsMouseDown(Button(button)) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL IsMouseClicked(int32_t button) { return ImGui::IsMouseClicked(Button(button)) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL IsMouseReleased(int32_t button) { return ImGui::IsMouseReleased(Button(button)) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL IsMouseDoubleClicked(int32_t button) { return ImGui::IsMouseDoubleClicked(Button(button)) ? SHARD_TRUE : SHARD_FALSE; }

        // SHARD_KEY_* -> ImGui's keys (the ones a panel needs)
        ImGuiKey KeyOf(int32_t key)
        {
            if (key >= SHARD_KEY_A && key <= SHARD_KEY_A + 25) return ImGuiKey(int(ImGuiKey_A) + (key - SHARD_KEY_A));
            if (key >= SHARD_KEY_0 && key <= SHARD_KEY_0 + 9) return ImGuiKey(int(ImGuiKey_0) + (key - SHARD_KEY_0));
            if (key >= SHARD_KEY_F1 && key <= SHARD_KEY_F1 + 11) return ImGuiKey(int(ImGuiKey_F1) + (key - SHARD_KEY_F1));
            switch (key) {
                case SHARD_KEY_SPACE: return ImGuiKey_Space;
                case SHARD_KEY_ENTER: return ImGuiKey_Enter;
                case SHARD_KEY_ESCAPE: return ImGuiKey_Escape;
                case SHARD_KEY_TAB: return ImGuiKey_Tab;
                case SHARD_KEY_BACKSPACE: return ImGuiKey_Backspace;
                case SHARD_KEY_DELETE: return ImGuiKey_Delete;
                case SHARD_KEY_INSERT: return ImGuiKey_Insert;
                case SHARD_KEY_LEFT: return ImGuiKey_LeftArrow;
                case SHARD_KEY_RIGHT: return ImGuiKey_RightArrow;
                case SHARD_KEY_UP: return ImGuiKey_UpArrow;
                case SHARD_KEY_DOWN: return ImGuiKey_DownArrow;
                case SHARD_KEY_HOME: return ImGuiKey_Home;
                case SHARD_KEY_END: return ImGuiKey_End;
                case SHARD_KEY_PAGE_UP: return ImGuiKey_PageUp;
                case SHARD_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
                case SHARD_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
                case SHARD_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
                case SHARD_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
                case SHARD_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
                case SHARD_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
                case SHARD_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
                default: return ImGuiKey_None;
            }
        }
        int32_t SHARD_CALL IsKeyDown(int32_t key) { const ImGuiKey k = KeyOf(key); return k != ImGuiKey_None && ImGui::IsKeyDown(k) ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL IsKeyPressed(int32_t key) { const ImGuiKey k = KeyOf(key); return k != ImGuiKey_None && ImGui::IsKeyPressed(k) ? SHARD_TRUE : SHARD_FALSE; }
        uint32_t SHARD_CALL GetModifiers()
        {
            const ImGuiIO& io = ImGui::GetIO();
            return (io.KeyShift ? SHARD_MOD_SHIFT : 0u) | (io.KeyCtrl ? SHARD_MOD_CONTROL : 0u) | (io.KeyAlt ? SHARD_MOD_ALT : 0u) | (io.KeySuper ? SHARD_MOD_SUPER : 0u);
        }
        int32_t SHARD_CALL IsWindowHovered() { return ImGui::IsWindowHovered() ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL IsWindowFocused() { return ImGui::IsWindowFocused() ? SHARD_TRUE : SHARD_FALSE; }

        void SHARD_CALL Image(uint64_t texture, float w, float h)
        {
            // A texture of shard.render : its native id exists once the render thread made it (the engine root knows)
            bool flipV = false;
            const uint64_t native = Engine::Plugins::RenderTextureForImGui(texture, &flipV);
            if (native) ImGui::Image(ImTextureID(native), ImVec2(w, h), ImVec2(0.0f, flipV ? 1.0f : 0.0f), ImVec2(1.0f, flipV ? 0.0f : 1.0f));      // a render target is bottom first
            else ImGui::Dummy(ImVec2(w, h));
        }
        int32_t SHARD_CALL SelectableSize(ShardStr l, int32_t selected, float w, float h) { return ImGui::Selectable(Z(l), selected != 0, 0, ImVec2(w, h)) ? SHARD_TRUE : SHARD_FALSE; }

        int32_t SHARD_CALL BeginDragDropSource() { return ImGui::BeginDragDropSource() ? SHARD_TRUE : SHARD_FALSE; }
        void SHARD_CALL SetDragDropPayload(ShardStr type, const void* data, uint64_t size)
        {
            std::string name = Z(type);
            if (name.size() > 32) name.resize(32);
            ImGui::SetDragDropPayload(name.c_str(), data, size_t(size));
        }
        void SHARD_CALL EndDragDropSource() { ImGui::EndDragDropSource(); }
        int32_t SHARD_CALL BeginDragDropTarget() { return ImGui::BeginDragDropTarget() ? SHARD_TRUE : SHARD_FALSE; }
        int32_t SHARD_CALL AcceptDragDropPayload(ShardStr type, void* out, uint64_t capacity, uint64_t* size)
        {
            std::string name = Z(type);
            if (name.size() > 32) name.resize(32);
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(name.c_str());
            if (!payload) return SHARD_FALSE;
            if (size) *size = uint64_t(payload->DataSize);
            if (out && capacity && payload->Data) std::memcpy(out, payload->Data, size_t(std::min<uint64_t>(capacity, uint64_t(payload->DataSize))));
            return SHARD_TRUE;
        }
        void SHARD_CALL EndDragDropTarget() { ImGui::EndDragDropTarget(); }

        ShardUiApi MakeWidgets()
        {
            ShardUiApi w;
            std::memset(&w, 0, sizeof(w));
            w.structSize = sizeof(ShardUiApi);
            w.text = &Text; w.text_colored = &TextColored; w.text_wrapped = &TextWrapped; w.bullet_text = &BulletText; w.label_text = &LabelText;
            w.button = &Button; w.small_button = &SmallButton; w.checkbox = &Checkbox; w.selectable = &Selectable; w.combo = &Combo;
            w.slider_float = &SliderFloat; w.slider_int = &SliderInt; w.drag_float = &DragFloat; w.drag_int = &DragInt;
            w.input_float = &InputFloat; w.input_int = &InputInt; w.input_text = &InputText; w.color_edit = &ColorEdit;
            w.progress_bar = &ProgressBar; w.plot_lines = &PlotLines;
            w.separator = &Separator; w.same_line = &SameLine; w.spacing = &Spacing; w.set_next_item_width = &SetNextItemWidth;
            w.collapsing_header = &CollapsingHeader; w.tree_node = &TreeNode; w.tree_pop = &TreePop; w.push_id = &PushId; w.pop_id = &PopId;
            w.is_item_hovered = &IsItemHovered; w.tooltip = &Tooltip;
            w.text_disabled = &TextDisabled; w.separator_text = &SeparatorText; w.radio_button = &RadioButton;
            w.drag_float2 = &DragFloat2; w.drag_float3 = &DragFloat3; w.drag_float4 = &DragFloat4; w.color_edit3 = &ColorEdit3;
            w.input_text_multiline = &InputTextMultiline; w.button_size = &ButtonSize; w.plot_histogram = &PlotHistogram;
            w.begin_group = &BeginGroup; w.end_group = &EndGroup; w.indent = &Indent; w.unindent = &Unindent; w.dummy = &Dummy; w.new_line = &NewLine;
            w.push_item_width = &PushItemWidth; w.pop_item_width = &PopItemWidth; w.begin_child = &BeginChild; w.end_child = &EndChild;
            w.begin_disabled = &BeginDisabled; w.end_disabled = &EndDisabled; w.push_text_color = &PushTextColor; w.pop_text_color = &PopTextColor;
            w.content_width = &ContentWidth;
            w.begin_tab_bar = &BeginTabBar; w.end_tab_bar = &EndTabBar; w.begin_tab_item = &BeginTabItem; w.end_tab_item = &EndTabItem;
            w.begin_table = &BeginTable; w.end_table = &EndTable; w.table_setup_column = &TableSetupColumn; w.table_headers_row = &TableHeadersRow;
            w.table_next_row = &TableNextRow; w.table_next_column = &TableNextColumn;
            w.begin_menu = &BeginMenu; w.end_menu = &EndMenu; w.menu_item = &MenuItem; w.open_popup = &OpenPopup; w.begin_popup = &BeginPopup;
            w.end_popup = &EndPopup; w.close_current_popup = &CloseCurrentPopup; w.begin_popup_context_item = &BeginPopupContextItem;
            w.is_item_clicked = &IsItemClicked; w.is_item_active = &IsItemActive; w.is_item_double_clicked = &IsItemDoubleClicked;
            w.draw_line = &DrawLine; w.draw_polyline = &DrawPolyline; w.draw_rect = &DrawRect; w.draw_rect_filled = &DrawRectFilled;
            w.draw_circle = &DrawCircle; w.draw_circle_filled = &DrawCircleFilled; w.draw_triangle_filled = &DrawTriangleFilled;
            w.draw_bezier = &DrawBezier; w.draw_text = &DrawText; w.push_clip_rect = &PushClipRect; w.pop_clip_rect = &PopClipRect;
            w.get_cursor_screen_pos = &GetCursorScreenPos; w.set_cursor_screen_pos = &SetCursorScreenPos; w.get_content_region_avail = &GetContentRegionAvail;
            w.get_item_rect_min = &GetItemRectMin; w.get_item_rect_max = &GetItemRectMax; w.calc_text_size = &CalcTextSize; w.invisible_button = &InvisibleButton;
            w.get_mouse_pos = &GetMousePos; w.get_mouse_drag_delta = &GetMouseDragDelta; w.reset_mouse_drag_delta = &ResetMouseDragDelta;
            w.get_mouse_wheel = &GetMouseWheel; w.is_mouse_down = &IsMouseDown; w.is_mouse_clicked = &IsMouseClicked; w.is_mouse_released = &IsMouseReleased;
            w.is_mouse_double_clicked = &IsMouseDoubleClicked; w.is_key_down = &IsKeyDown; w.is_key_pressed = &IsKeyPressed; w.get_modifiers = &GetModifiers;
            w.is_window_hovered = &IsWindowHovered; w.is_window_focused = &IsWindowFocused; w.image = &Image; w.selectable_size = &SelectableSize;
            w.begin_drag_drop_source = &BeginDragDropSource; w.set_drag_drop_payload = &SetDragDropPayload; w.end_drag_drop_source = &EndDragDropSource;
            w.begin_drag_drop_target = &BeginDragDropTarget; w.accept_drag_drop_payload = &AcceptDragDropPayload; w.end_drag_drop_target = &EndDragDropTarget;
            return w;
        }

        const ShardUiApi kWidgets = MakeWidgets();

        // The notifications and the selection of the editor, for shard.editor
        Core::EditorMainWindow* gWindow = nullptr;
        void Notify(uint32_t level, const char* text)
        {
            if (level == SHARD_EDITOR_NOTIFY_ERROR) Notifications::Error("%s", text);
            else if (level == SHARD_EDITOR_NOTIFY_WARNING) Notifications::Warning("%s", text);
            else Notifications::Info("%s", text);
        }
        std::vector<std::shared_ptr<Engine::Objects::Actor>> Selection()
        {
            return gWindow ? gWindow->GetSelectedActors() : std::vector<std::shared_ptr<Engine::Objects::Actor>>();
        }

        bool BeginWindow(const char* title, bool* open) { return ImGui::Begin(title, open); }
        void EndWindow() { ImGui::End(); }

        // An edit a plugin made, in the editor's history
        class PluginCommand final : public Commands::Command {
        public:
            PluginCommand(std::string label, std::function<void()> undo, std::function<void()> redo) : m_Name(std::move(label)), m_Undo(std::move(undo)), m_Redo(std::move(redo)) {}
            void Undo() override { if (m_Undo) m_Undo(); }
            void Redo() override { if (m_Redo) m_Redo(); }
            const std::string& GetName() const override { return m_Name; }
        private:
            std::string m_Name;
            std::function<void()> m_Undo, m_Redo;
        };

        void PushUndo(const std::string& label, std::function<void()> undo, std::function<void()> redo)
        {
            Commands::CommandStack::Get().Record(std::make_unique<PluginCommand>(label, std::move(undo), std::move(redo)));
        }

        const Engine::Plugins::EditorUiBackend kBackend = { &kWidgets, &BeginWindow, &EndWindow, &Notify, &Selection, &PushUndo };
    }

    namespace {
        using Engine::Plugins::EditorMenuItem;

        // "Tools/Bake/All" -> the part before the first '/' and the rest
        std::pair<std::string, std::string> SplitPath(const std::string& path)
        {
            const size_t slash = path.find('/');
            return slash == std::string::npos ? std::make_pair(path, std::string()) : std::make_pair(path.substr(0, slash), path.substr(slash + 1));
        }

        struct MenuEntry {
            std::string rest;
            std::shared_ptr<EditorMenuItem> item;
        };

        // Items whose paths (relative to the menu being drawn) are `entries`: a path with one part is an item, with more it is under a submenu of its first part
        void DrawEntries(const std::vector<MenuEntry>& entries)
        {
            std::vector<std::string> submenus;
            for (const MenuEntry& e : entries) {
                const auto [head, rest] = SplitPath(e.rest);
                if (rest.empty()) {
                    if (ImGui::MenuItem(head.c_str(), e.item->shortcut.empty() ? nullptr : e.item->shortcut.c_str()) && e.item->run) e.item->run();
                } else if (std::find(submenus.begin(), submenus.end(), head) == submenus.end()) {
                    submenus.push_back(head);
                    std::vector<MenuEntry> inside;
                    for (const MenuEntry& other : entries) {
                        const auto [otherHead, otherRest] = SplitPath(other.rest);
                        if (otherHead == head && !otherRest.empty()) inside.push_back({ otherRest, other.item });
                    }
                    if (ImGui::BeginMenu(head.c_str())) {
                        DrawEntries(inside);
                        ImGui::EndMenu();
                    }
                }
            }
        }
    }

    void DrawPluginMenuItems(const char* menu, bool separator)
    {
        std::vector<MenuEntry> entries;
        for (const auto& item : Engine::Plugins::PluginManager::Get().Extensions().Get<EditorMenuItem>("editor.menu")) {
            const auto [top, rest] = SplitPath(item->path);
            if (top == menu && !rest.empty()) entries.push_back({ rest, item });
        }
        if (entries.empty()) return;
        if (separator) ImGui::Separator();
        DrawEntries(entries);
    }

    std::vector<std::string> PluginMenuNames()
    {
        static const char* builtin[] = { "File", "Edit", "Select", "Tools", "Window", "Help" };
        std::vector<std::string> names;
        for (const auto& item : Engine::Plugins::PluginManager::Get().Extensions().Get<EditorMenuItem>("editor.menu")) {
            const std::string top = SplitPath(item->path).first;
            if (std::find(std::begin(builtin), std::end(builtin), top) == std::end(builtin) && std::find(names.begin(), names.end(), top) == names.end()) names.push_back(top);
        }
        return names;
    }

    void DrawPluginToolbar()
    {
        for (const auto& item : Engine::Plugins::PluginManager::Get().Extensions().Get<Engine::Plugins::EditorToolbarItem>("editor.toolbar")) {
            ImGui::SameLine();
            if (ImGui::Button(item->label.c_str()) && item->run) item->run();
            if (!item->tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", item->tooltip.c_str());
        }
    }

    void DrawPluginInspectors(const std::string& componentType, const std::shared_ptr<Engine::Objects::Actor>& actor)
    {
        if (!actor) return;
        for (const auto& item : Engine::Plugins::PluginManager::Get().Extensions().Get<Engine::Plugins::EditorInspectorItem>("editor.inspector")) {
            if (item->componentType != componentType || !item->draw) continue;
            ImGui::PushID(item.get());
            item->draw(&kWidgets, Engine::Plugins::EditorActorHandle(actor));
            ImGui::PopID();
        }
    }

    void InstallPluginUi(Core::EditorMainWindow* window) { gWindow = window; Engine::Plugins::SetEditorUiBackend(&kBackend); }
    void RemovePluginUi() { Engine::Plugins::SetEditorUiBackend(nullptr); gWindow = nullptr; }

    const ShardUiApi* PluginWidgets() { return &kWidgets; }
}
