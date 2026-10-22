#include "plugin_ui.hpp"

#include <string>
#include <vector>

#include "imgui/imgui.h"

#include <shard/shard_editor.h>

#include "engine/plugins/abi/c_plugin.hpp"

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

        const ShardUiApi kWidgets = {
            sizeof(ShardUiApi),
            &Text, &TextColored, &TextWrapped, &BulletText, &LabelText,
            &Button, &SmallButton, &Checkbox, &Selectable, &Combo,
            &SliderFloat, &SliderInt, &DragFloat, &DragInt, &InputFloat, &InputInt, &InputText, &ColorEdit,
            &ProgressBar, &PlotLines,
            &Separator, &SameLine, &Spacing, &SetNextItemWidth, &CollapsingHeader, &TreeNode, &TreePop, &PushId, &PopId,
            &IsItemHovered, &Tooltip };

        bool BeginWindow(const char* title, bool* open) { return ImGui::Begin(title, open); }
        void EndWindow() { ImGui::End(); }

        const Engine::Plugins::EditorUiBackend kBackend = { &kWidgets, &BeginWindow, &EndWindow };
    }

    void InstallPluginUi() { Engine::Plugins::SetEditorUiBackend(&kBackend); }
    void RemovePluginUi() { Engine::Plugins::SetEditorUiBackend(nullptr); }
}
