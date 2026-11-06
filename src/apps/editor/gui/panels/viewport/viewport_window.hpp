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

            void FocusSelected();
            void SnapSelectedToFloor();

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

            void HandleShortcuts();
            bool HandleMarquee(bool canStart);
            void SelectInRect(const ImVec2& a, const ImVec2& b, bool toggle);
            void ApplyPick(const std::shared_ptr<Engine::Objects::Actor>& actor);
            std::shared_ptr<Engine::Objects::Actor> PickActor(float localX, float localY);


            Core::EditorMainWindow* parent = nullptr;

            glm::vec2 viewportSize = glm::vec2(50, 50);
            ImVec2 viewportPos = ImVec2(0,0);
            bool viewportHovered = false;
            bool viewportFocused = false;
            bool gameViewShown = false;

            std::shared_ptr<Engine::Objects::Actor> cameraActor;

            std::shared_ptr<Engine::Objects::Components::Camera> camera;

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

            Engine::Rendering::Raytracing::RaytraceSettings raytraceSettings;
            char raytraceOutputPath[256] = "render.hdr";
            std::unique_ptr<Engine::Rendering::Raytracing::Raytracer> raytracer;
            std::string raytraceStatusMessage;

            float firstClickTime = 0;

            bool focusAnimating = false;
            glm::vec3 focusTarget = glm::vec3(0);

            ImGuizmo::OPERATION currentGizmoOp = ImGuizmo::TRANSLATE;
            ImGuizmo::MODE currentGizmoMode = ImGuizmo::LOCAL;

            std::unique_ptr<Commands::CompositeCommand> gizmoCommand;

            bool isUsingViewGizmo = false;

            bool marqueePending = false;
            bool marqueeActive = false;
            ImVec2 marqueeStart = ImVec2(0, 0);
            ImVec2 marqueeEnd = ImVec2(0, 0);

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