#pragma once

#include "engine/platform/devices/iinput.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/event_system.hpp"

#include "apps/editor/commands/command_stack.hpp"
#include "apps/editor/commands/actor_commands.hpp"

#include "engine/renderer/features/raytracing/raytracer.hpp"
#include "engine/renderer/components/camera.hpp"

#include "imgui/imgui.h"
#include "ImGuizmo.h"

namespace Shard::Editor::Core{
    class EditorMainWindow;
}

namespace Shard::Editor::GUI {

    class ViewportWindow
    {
        public:
            void Draw();
            void ProcessInputs();

            ViewportWindow();

            ~ViewportWindow();

            glm::vec2 GetViewportSize() const { return viewportSize; }
            bool IsHovered() const { return viewportHovered; }

            void SetParentWindow(Core::EditorMainWindow* parent);

            void SetGizmoOperation(ImGuizmo::OPERATION op) { currentGizmoOp = op; }
            ImGuizmo::OPERATION GetGizmoOperation() const { return currentGizmoOp; }

            /// @brief Moves the editor camera so the selected actor (and its children) fills the view (F)
            void FocusSelected();
            /// @brief Drops the selected actor straight down onto whatever lies below it (End)
            void SnapSelectedToFloor();

            // Store previous size
            static ImVec2 prev_size;
            
        private:
            void DrawToolbar();
            void DrawSnapControls();
            void DrawViewGizmo();
            void DrawObjectGizmo();
            void HandleAssetDrop();
            void ShowFrameStats();
            void ShowCamSettings();
            void ShowViewportVisSettings();
            void ShowRaytraceSettings();

            /// Delete, Ctrl+C / X / V / D / A / G, F2 and Escape on the selection, while the viewport has the focus
            void HandleShortcuts();
            /// Ctrl or Shift + left button drags a selection rectangle (a click without moving picks). Returns true while it
            /// uses the mouse (the camera does not look around then).
            bool HandleMarquee(bool canStart);
            void SelectInRect(const ImVec2& a, const ImVec2& b, bool toggle);
            /// What the click that picked this actor does to the selection (Ctrl toggles it, Shift adds it, else it replaces it)
            void ApplyPick(const std::shared_ptr<Engine::Objects::Actor>& actor);
            /// The actor under the pixel (relative to the viewport image), null if none
            std::shared_ptr<Engine::Objects::Actor> PickActor(float localX, float localY);


            Core::EditorMainWindow* parent = nullptr;

            glm::vec2 viewportSize = glm::vec2(50, 50);
            ImVec2 viewportPos = ImVec2(0,0);
            bool viewportHovered = false;
            bool viewportFocused = false;

            // Camera
            std::shared_ptr<Engine::Objects::Actor> cameraActor;

            std::shared_ptr<Engine::Objects::Components::Camera> camera;

            // Speed, sensitivity and lens : the editor's preferences (EditorPreferences::camera)
            float pitch = 0.0f;
            float yaw = 0.0f;

            bool firstClick = true;
            double lockedMouseX = 0.0;
            double lockedMouseY = 0.0;

            bool gizmoActive = false;
            bool showFrameStats = false;
            bool showCamSettings = false;
            bool showViewportVisibility = false;
            bool showRaytraceSettings = false;
            bool uiHovered = false;

            // Offline raytrace
            Engine::Rendering::Raytracing::RaytraceSettings raytraceSettings;
            char raytraceOutputPath[256] = "render.hdr";
            std::unique_ptr<Engine::Rendering::Raytracing::Raytracer> raytracer;
            std::string raytraceStatusMessage;

            float firstClickTime = 0;

            // Camera focus animation (see FocusSelected)
            bool focusAnimating = false;
            glm::vec3 focusTarget = glm::vec3(0);

            // Gizmos
            ImGuizmo::OPERATION currentGizmoOp = ImGuizmo::TRANSLATE;
            ImGuizmo::MODE currentGizmoMode = ImGuizmo::LOCAL;

            std::unique_ptr<Commands::CompositeCommand> gizmoCommand;

            bool isUsingViewGizmo = false;

            // Selection rectangle (screen coordinates)
            bool marqueePending = false;
            bool marqueeActive = false;
            ImVec2 marqueeStart = ImVec2(0, 0);
            ImVec2 marqueeEnd = ImVec2(0, 0);

            // The gizmo drags the primary actor ; with several selected the others follow, each from where it was
            // when the drag began
            struct GizmoTarget {
                std::shared_ptr<Engine::Objects::Actor> actor;
                glm::mat4 startWorld;
            };
            std::vector<GizmoTarget> gizmoTargets;
            glm::mat4 gizmoStartMatrix = glm::mat4(1.0f);

            ImVec2 viewportImageMin;
            ImVec2 viewportImageMax;
    };
}