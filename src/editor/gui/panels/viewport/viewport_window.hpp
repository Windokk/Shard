#pragma once

#include "editor/core/platform/glfw/glfw_input.hpp"
#include "engine/objects/actors/actor.hpp"
#include "engine/events/event_system.hpp"

#include "editor/commands/command_stack.hpp"

#include "engine/rendering/raytracing/raytracer.hpp"

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


            Core::EditorMainWindow* parent = nullptr;

            glm::vec2 viewportSize = glm::vec2(50, 50);
            ImVec2 viewportPos = ImVec2(0,0);
            bool viewportHovered = false;
            bool viewportFocused = false;

            // Camera
            std::shared_ptr<Engine::Objects::Actor> cameraActor;

            std::shared_ptr<Engine::Objects::Components::Camera> camera;

            float speed = 20.0f;
            float pitch = 0.0f;
            float yaw = 0.0f;
            float mouseSensitivity = 0.1f;

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

            // Gizmos
            ImGuizmo::OPERATION currentGizmoOp = ImGuizmo::TRANSLATE;
            ImGuizmo::MODE currentGizmoMode = ImGuizmo::LOCAL;

            std::unique_ptr<Commands::CompositeCommand> gizmoCommand;

            bool isUsingViewGizmo = false;

            ImVec2 viewportImageMin;
            ImVec2 viewportImageMax;
    };
}