#include "viewport_window.hpp"

#include "engine/core/engine.hpp"
#include "engine/core/platform/iplatform.hpp"
#include "engine/levels/level_manager.hpp"
#include "engine/objects/components/misc/transform.reflection.hpp"

#include "editor/gui/IconsLucide.h"
#include "editor/gui/main_window.hpp"
#include "editor/gui/panels/common.hpp"
#include "editor/gui/imoguizmo.hpp"
#include "editor/gui/dragdrop/asset_drag_drop.hpp"

#include "engine/objects/components/rendering/model_component.hpp"
#include "engine/objects/components/physics/physics_body.hpp"


#include <glm/gtx/string_cast.hpp>

#include <algorithm>
#include <cfloat>
#include <cstdio>

#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>

#include "engine/debugging/profiler.hpp"
#include "engine/time/time_manager.hpp"

namespace {

    // Slab-method ray-vs-AABB test. `dir` is assumed normalized. Only reports a hit closer
    // than maxDistance (so callers can pass the current-best hit distance to short-circuit).
    bool RayIntersectsAABB(const glm::vec3& origin, const glm::vec3& dir,
                            const glm::vec3& boundsMin, const glm::vec3& boundsMax,
                            float maxDistance, float& outT)
    {
        float tMin = 0.0f;
        float tMax = maxDistance;

        for (int axis = 0; axis < 3; ++axis)
        {
            float invD = 1.0f / dir[axis];
            float t0 = (boundsMin[axis] - origin[axis]) * invD;
            float t1 = (boundsMax[axis] - origin[axis]) * invD;

            if (invD < 0.0f)
                std::swap(t0, t1);

            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);

            if (tMax < tMin)
                return false;
        }

        outT = tMin;
        return true;
    }

    // World-space AABB of a mesh's local bounds transformed by worldMatrix. Recomputed from
    // scratch every call (only used on click, not per-frame) rather than cached, since the
    // actor's world matrix can change between clicks without any picking-relevant event firing.
    void ComputeWorldBounds(const glm::vec3& localMin, const glm::vec3& localMax,
                             const glm::mat4& worldMatrix, glm::vec3& worldMin, glm::vec3& worldMax)
    {
        worldMin = glm::vec3(FLT_MAX);
        worldMax = glm::vec3(-FLT_MAX);

        for (int i = 0; i < 8; ++i)
        {
            glm::vec3 corner(
                (i & 1) ? localMax.x : localMin.x,
                (i & 2) ? localMax.y : localMin.y,
                (i & 4) ? localMax.z : localMin.z);

            glm::vec3 worldCorner = glm::vec3(worldMatrix * glm::vec4(corner, 1.0f));
            worldMin = glm::min(worldMin, worldCorner);
            worldMax = glm::max(worldMax, worldCorner);
        }
    }

}

namespace Shard::Editor::GUI {

    ImVec2 ViewportWindow::prev_size = ImVec2(0, 0);

    void ViewportWindow::Draw()
    {
        // The editor fly-camera must own the viewport whenever we're not in play mode. Loading or
        // reloading a level rebinds CameraManager's active camera through Level::OnLoad() - to a
        // level camera, or to nothing at all when the scene has no camera actor, which left the
        // viewport with no active camera ("Failed to find a valid active camera"). Re-assert it
        // here every editor frame; it's a cheap lookup that no-ops once we're already active.
        if (cameraActor && !Engine::Core::GetEngine().IsInPlayMode())
        {
            auto* cameraManager = Engine::Core::GetEngine().GetCameraManager();
            if (cameraManager->GetActiveCamera() != camera)
            {
                if (!cameraManager->GetCamera(cameraActor->GetID()))
                    cameraManager->AddCamera(cameraActor->GetID(), camera);
                cameraManager->SetActiveCamera(cameraActor->GetID());
            }
        }

        // Dispatches one accumulated sample (or, on the first call after Start(), builds the scene/BVH)
        // per editor frame while a raytrace is active (see Raytracer::Update()'s comments) - called
        // unconditionally, regardless of whether the raytrace settings window is currently open, so the
        // render keeps progressing in the background even if the user closes that panel.
        if (raytracer && raytracer->IsActive())
            raytracer->Update();

        ImGui::Begin("Viewport");

        float toolbarHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2;
        ImGui::BeginChild("ToolbarArea", ImVec2(0, toolbarHeight), false, ImGuiWindowFlags_NoScrollbar);
        DrawToolbar();
        ImGui::EndChild();
        
        // Get current size
        ImVec2 current_size = ImGui::GetWindowSize();

        // Rendu texture framebuffer
        uint32_t textureID = Engine::Core::GetEngine().GetRenderer()->GetViewportTextureHandle();

        ImVec2 avail = ImGui::GetContentRegionAvail();
        viewportSize = glm::vec2(avail.x, avail.y);

        if ((current_size.x != prev_size.x || current_size.y != prev_size.y)
            && avail.x > 0 && avail.y > 0)
        {
            Engine::Core::GetEngine().GetRenderer()->RescaleFramebuffers(avail.x, avail.y);
        }

        // Save for next frame
        prev_size = current_size;

        if (textureID != 0)
        {
            ImGui::Image(
                (void*)(intptr_t)textureID,
                ImVec2(viewportSize.x, viewportSize.y),
                ImVec2(0, 1),
                ImVec2(1, 0)
            );
        }

        viewportHovered = ImGui::IsItemHovered();
        viewportFocused = ImGui::IsWindowFocused();

        viewportImageMin = ImGui::GetItemRectMin();
        viewportImageMax = ImGui::GetItemRectMax();

        HandleAssetDrop();

        if(parent && parent->GetSelectedActor() && parent->settings.showGizmos)
            DrawObjectGizmo();

        viewportPos = ImGui::GetWindowPos();

        ImGui::SameLine();

        if(parent->settings.showGizmos)
            DrawViewGizmo();

        uiHovered = ImGui::IsAnyItemHovered();
    
        ImGui::End();

    }

    void ViewportWindow::SetParentWindow(Core::EditorMainWindow *parent)
    {
        this->parent = parent;

        cameraActor = Engine::Core::Object::CreateWithContext<Engine::Objects::Actor>(&Engine::Core::GetEngine(), "[EDITOR] Camera", &Engine::Core::GetEngine());
        cameraActor->AddComponent<Engine::Objects::Components::Camera>();
        int width = Engine::Core::GetEngine().GetWindow()->GetFramebufferWidth();
        int height = Engine::Core::GetEngine().GetWindow()->GetFramebufferHeight();
        cameraActor->GetComponent<Engine::Objects::Components::Camera>()->Init(width, height, 0.1f, 100.0f, 60, false, 10);
        Engine::Core::GetEngine().GetCameraManager()->AddCamera(cameraActor->GetID(), cameraActor->GetComponent<Engine::Objects::Components::Camera>());
        Engine::Core::GetEngine().GetCameraManager()->SetActiveCamera(cameraActor->GetID());

        cameraActor->transform->SetPosition(glm::vec3(10, 0, 0));

        camera = cameraActor->GetComponent<Engine::Objects::Components::Camera>();

        camera->UpdateMatrix();
    }

    void ViewportWindow::DrawToolbar()
    {
        ImGui::SetCursorPos(ImVec2(10, 0));

        float toolbarWidth = ImGui::GetContentRegionAvail().x;

        // Gizmo Space
        const char* items[2] = { ICON_LC_LOCATE " Local  ", ICON_LC_EARTH " World  " };
        static int item_selected_idx = 0;
        const char* combo_preview_value = items[item_selected_idx];
        if (ImGui::BeginCombo("##GizmoSpace", combo_preview_value, ImGuiComboFlags_WidthFitPreview))
        {
            for (int n = 0; n < IM_ARRAYSIZE(items); n++)
            {
                const bool is_selected = (item_selected_idx == n);
                if (ImGui::Selectable(items[n], is_selected))
                    item_selected_idx = n;
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        currentGizmoMode = (item_selected_idx == 0) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

        ImGui::SameLine();

        // Gizmo Operations (packed to the left)
        auto DrawGizmoButton = [&](const char* icon, ImGuizmo::OPERATION op)
        {
            bool selected = (currentGizmoOp == op);
            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(icon)) currentGizmoOp = op;
            if (selected) ImGui::PopStyleColor();
        };

        DrawGizmoButton(ICON_LC_MOVE, ImGuizmo::TRANSLATE);
        ImGui::SameLine();
        DrawGizmoButton(ICON_LC_REFRESH_CW, ImGuizmo::ROTATE);
        ImGui::SameLine();
        DrawGizmoButton(ICON_LC_SCALING, ImGuizmo::SCALE);

        // Snap toggle + increment for the selected tool, right next to the tool buttons
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 3.0f);
        DrawSnapControls();

        ImGui::SameLine();

        float playButtonWidth = ImGui::CalcTextSize(ICON_LC_PLAY).x + ImGui::GetStyle().FramePadding.x * 2;
        float centerX = std::max(0.0f, (toolbarWidth - playButtonWidth) / 2.0f);
        // Never centre the play button on top of the left-hand controls in a narrow viewport
        ImGui::SetCursorPosX(std::max(centerX, ImGui::GetCursorPosX()));

        if (Engine::Core::GetEngine().IsInPlayMode())
        {
            if (ImGui::Button(ICON_LC_SQUARE)){
                Engine::Core::GetEngine().SetPlayMode(false);
                Engine::Core::GetEngine().GetCameraManager()->SetActiveCamera(cameraActor->GetID());
            }
        }
        else
        {
            if (ImGui::Button(ICON_LC_PLAY))
            {
                parent->SetSelectedActor(nullptr);
                Engine::Core::GetEngine().SetPlayMode(true);
            }
        }

        
        ImGui::SameLine();
        
        float frameStatsWidth = ImGui::CalcTextSize(ICON_LC_CHART_PIE).x + ImGui::GetStyle().FramePadding.x * 2;
        float cameraWidth = ImGui::CalcTextSize(ICON_LC_CAMERA).x + ImGui::GetStyle().FramePadding.x * 2;
        float eyeWidth = ImGui::CalcTextSize(ICON_LC_EYE_OFF).x + ImGui::GetStyle().FramePadding.x * 2;
        float raytraceWidth = ImGui::CalcTextSize(ICON_LC_APERTURE).x + ImGui::GetStyle().FramePadding.x * 2;
        float rightX = toolbarWidth - (frameStatsWidth + cameraWidth + eyeWidth + raytraceWidth + 16); // spacing
        ImGui::SetCursorPosX(rightX);

        // Frame Stats Button
        {
            if(showFrameStats)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            else
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
            if(ImGui::Button(ICON_LC_CHART_PIE))
                showFrameStats = !showFrameStats;
            ImGui::PopStyleColor();

            if(showFrameStats)
            {
                ShowFrameStats();
            }
        }

        ImGui::SameLine();

        // Camera settings
        {
            if(showCamSettings)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            else
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
            if(ImGui::Button(ICON_LC_CAMERA))
                showCamSettings = !showCamSettings;
            ImGui::PopStyleColor();

            if(showCamSettings)
            {
                ShowCamSettings();
            }
        }

        ImGui::SameLine();

        // Viewport visibility settings
        {
            if(showViewportVisibility)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            else
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
            if(ImGui::Button(showViewportVisibility ? ICON_LC_EYE : ICON_LC_EYE_OFF))
                showViewportVisibility = !showViewportVisibility;
            ImGui::PopStyleColor();

            if(showViewportVisibility)
            {
                ShowViewportVisSettings();
            }
        }

        ImGui::SameLine();

        // Offline raytrace
        {
            if(showRaytraceSettings)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            else
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
            if(ImGui::Button(ICON_LC_APERTURE))
                showRaytraceSettings = !showRaytraceSettings;
            ImGui::PopStyleColor();

            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("Offline raytrace");

            if(showRaytraceSettings)
            {
                ShowRaytraceSettings();
            }
        }
    }

    void ViewportWindow::DrawViewGizmo(){
        ImVec2 gizmoPos = ImGui::GetCursorScreenPos();
        gizmoPos.x -= 128;

        ImOGuizmo::SetRect(gizmoPos.x, gizmoPos.y, 120.0f);

        static glm::mat4 gizmoProj = glm::perspective(glm::radians(90.0f), 4/3.0f, 0.01f, 1000.0f);

        auto cam = Engine::Core::GetEngine().GetCameraManager()->GetActiveCamera();

        glm::mat4 view = cam->GetView();

        int axis = ImOGuizmo::DrawGizmo(glm::value_ptr(view), glm::value_ptr(gizmoProj), 0.1f);
        if(axis != -1 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            glm::vec3 euler;
            switch(axis)
            {
                case 0: euler = glm::vec3(0, 90, 0); break;
                case 1: euler = glm::vec3(-90, 0, 0); break;
                case 2: euler = glm::vec3(0, 180, 0); break;
                case 3: euler = glm::vec3(0, -90, 0); break;
                case 4: euler = glm::vec3(90, 0, 0); break;
                case 5: euler = glm::vec3(0, 0, 0); break;
            }
            cam->parent->transform->SetRotation(glm::quat(glm::radians(euler)));

            pitch = euler.x;
            yaw = euler.y;

            isUsingViewGizmo = true;
        }
        else if(axis != -1 && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            isUsingViewGizmo = true;
        }
        else{
            isUsingViewGizmo = false;
        }
    }

    void ViewportWindow::DrawObjectGizmo()
    {
        // Object gizmo

        auto selected = parent->GetSelectedActor();
        if (!selected)
            return;
            
        auto cam = Engine::Core::GetEngine().GetCameraManager()->GetActiveCamera();

        ImGuizmo::SetDrawlist();
        ImGuizmo::SetOrthographic(cam->IsOrthographic());

        ImVec2 imageSize = ImVec2(
            viewportImageMax.x - viewportImageMin.x,
            viewportImageMax.y - viewportImageMin.y
        );

        ImGuizmo::SetRect(
            viewportImageMin.x,
            viewportImageMin.y,
            imageSize.x,
            imageSize.y
        );
        glm::mat4 view = cam->GetView();
        glm::mat4 proj = cam->GetProjection();

        // ImGuizmo manipulates in world space (it's driven by the camera's world-space view matrix) -
        // so hand it the actor's world matrix, and convert back to local when writing the result back.
        glm::mat4 transform = selected->transform->GetWorldMatrix();

        // Only the active mode's increment is handed to ImGuizmo (nullptr = free movement). Translate
        // reads one step per axis, rotate (degrees) and scale only read the first component.
        const auto& settings = parent->settings;
        float snapValues[3] = { 0.0f, 0.0f, 0.0f };
        const float* snap = nullptr;

        auto ApplySnap = [&](bool enabled, float step)
        {
            if (!enabled)
                return;
            snapValues[0] = snapValues[1] = snapValues[2] = step;
            snap = snapValues;
        };

        switch (currentGizmoOp)
        {
            case ImGuizmo::TRANSLATE: ApplySnap(settings.snapLocation, settings.locationSnap); break;
            case ImGuizmo::ROTATE:    ApplySnap(settings.snapRotation, settings.rotationSnap); break;
            case ImGuizmo::SCALE:     ApplySnap(settings.snapScale,    settings.scaleSnap);    break;
            default: break;
        }

        ImGuizmo::Manipulate(
            glm::value_ptr(view),
            glm::value_ptr(proj),
            currentGizmoOp,
            currentGizmoMode,
            glm::value_ptr(transform),
            nullptr,
            snap
        );

        if (ImGuizmo::IsUsing() && !gizmoActive)
        {
            gizmoActive = true;

            auto& stack = Commands::CommandStack::Get();
            stack.Begin("Transform Object");

            auto id = selected->transform->GetID().GetAsInt();

            auto resolver = [](uint32_t id) -> void*
            {
                return Engine::Core::GetEngine().GetObjectIDManager()->GetObjectFromID(Engine::Core::ObjectID(id)).get();
            };

            FieldInfo* positionField = selected->transform->GetDescriptor()->fields[0];
            FieldInfo* rotationField = selected->transform->GetDescriptor()->fields[1];
            FieldInfo* scaleField    = selected->transform->GetDescriptor()->fields[2];

            stack.Add(std::make_unique<Commands::ModifyFieldCommand>(
                id, resolver, positionField, selected->transform));

            stack.Add(std::make_unique<Commands::ModifyFieldCommand>(
                id, resolver, rotationField, selected->transform));

            stack.Add(std::make_unique<Commands::ModifyFieldCommand>(
                id, resolver, scaleField, selected->transform));
        }

        if (ImGuizmo::IsUsing()) {
            selected->transform->SetFromWorldMatrix(transform);
        }

        if (!ImGuizmo::IsUsing() && gizmoActive) {
            Commands::CommandStack::Get().End();
            gizmoActive = false;

            if (auto* level = Engine::Core::GetEngine().GetLevelManager()->GetLevelAt(0))
                level->SetDirty(true);
        }
    }

    void ViewportWindow::HandleAssetDrop()
    {
        if (!ImGui::BeginDragDropTarget())
            return;

        std::vector<std::string> dropped = DragDrop::AcceptAssetDragDropPayload();
        if (!dropped.empty())
        {
            auto level = Engine::Core::GetEngine().GetLevelManager()->GetLevelAt(0);
            if (level)
            {
                // Ray from the camera through the drop position - if it lands on existing geometry the
                // new actor spawns there (dropping onto a floor/wall places it flush against it),
                // otherwise a fixed distance out in front of the camera so it never spawns behind it.
                ImVec2 mouse = ImGui::GetMousePos();
                float localX = mouse.x - viewportImageMin.x;
                float localY = mouse.y - viewportImageMin.y;

                glm::vec3 nearPoint = camera->ScreenToWorld(localX, localY, 0.0f);
                glm::vec3 farPoint  = camera->ScreenToWorld(localX, localY, 1.0f);
                glm::vec3 dir = glm::normalize(farPoint - nearPoint);

                Engine::Physics::RaycastResult hit = Engine::Core::GetEngine().GetPhysicsManager()->RayCast({nearPoint, dir, 10000.0f});
                glm::vec3 spawnPos = nearPoint + dir * (hit.hit ? hit.hitDistance : 15.0f);

                std::shared_ptr<Engine::Objects::Actor> lastSpawned;

                for (const auto& nameInProject : dropped)
                {
                    // Only static meshes can be dropped directly into the scene this way - other asset
                    // types (materials, textures, ...) are assigned onto existing fields instead (see
                    // the Asset-field/material-slot drag targets in properties_panel.cpp / material_editor_panel.cpp).
                    if (Engine::Filesystem::Path(nameInProject).GetExtensionType() != Engine::Filesystem::Type::T_MODEL)
                        continue;

                    auto actor = Engine::Core::Object::CreateWithContext<Engine::Objects::Actor>(
                        &Engine::Core::GetEngine(),
                        Engine::Filesystem::Path(nameInProject).GetFilename(false),
                        &Engine::Core::GetEngine());

                    level->AddActor(actor);
                    actor->transform->SetPosition(spawnPos);

                    auto model = actor->AddComponent<Engine::Objects::Components::Model>();
                    if (model)
                        model->SetMesh(nameInProject);

                    lastSpawned = actor;
                }

                if (lastSpawned && parent)
                    parent->SetSelectedActor(lastSpawned);
            }
        }

        ImGui::EndDragDropTarget();
    }

    void ViewportWindow::ShowFrameStats(){

        ImGui::Begin("Statistics", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);

        // Display frame metrics
        Engine::Debugging::MinimalStatistics stats = Engine::Core::GetEngine().GetProfiler()->GetStats();
        static Engine::Core::Platform::SystemInfos system;
        
        if(ImGui::GetFrameCount() % 5 == 0)
        {
            system = Engine::Core::GetEngine().GetWindow()->GetSystemInfos();
        }

        ImGui::Text("Sounds: %d", stats.sounds);

        ImGui::Separator();

        // Rendering stats
        ImGui::Text("Frame Time: %.2f ms (%.1f FPS)", stats.frameTimeMs, stats.fps);
        ImGui::Text("Draw Calls: %d", stats.cmds);
        ImGui::Text("Primitives: %d", stats.primitives);
        ImGui::Text("Vertices: %d", stats.vertices);
        ImGui::Text("GPU Memory: %.1f MB", stats.gpuMemoryMB);

        ImGui::Separator();

        // Level stats
        ImGui::Text("Actors: %d", stats.actors);
        ImGui::Text("Lights: %d", stats.lights);

        ImGui::Separator();

        std::string text = "Renderer : " + system.gpu_renderer;
        TextEllipsis(text.c_str());
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", system.gpu_renderer.c_str());
        }

        ImGui::End();
    }

    void ViewportWindow::ShowCamSettings(){
        
        ImGui::Begin("Scene Camera", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        std::shared_ptr<Engine::Objects::Components::Camera> cam = cameraActor->GetComponent<Engine::Objects::Components::Camera>();

        // Rendering stats
        ImGui::Text("Camera width : %f px", cam->GetSize().x);
        ImGui::Text("Camera height : %f px", cam->GetSize().y);

        ImGui::Separator();

        ImGui::SliderFloat("Camera speed", &speed, 0, 100);
        ImGui::SliderFloat("Mouse sensitivity", &mouseSensitivity, 0, 10);

        ImGui::Separator();

        ImGui::SliderFloat("Near plane", &cam->nearPlane, 0.01f, 1000);
        ImGui::SliderFloat("Far plane", &cam->farPlane, cam->nearPlane + 1, 1000);

        ImGui::Checkbox("Ortho ?", &cam->orthographic);

        if(cam->IsOrthographic()){
            ImGui::SliderFloat("Ortho size", cam->GetOrthoSize(), 1.0f, 100.0f);
        }
        else{
            ImGui::SliderFloat("FOV", cam->GetFOV(), 1.0f, 150.0f);
        }

        ImGui::End();
    }

    void ViewportWindow::ShowViewportVisSettings()
    {
        ImGui::Begin("Visibility", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Separator();

        ImGui::Checkbox("Show Outlines ?", &parent->settings.showOutlines);
        ImGui::Checkbox("Show Gizmos ?", &parent->settings.showGizmos);

        ImGui::Checkbox("Show DDGI Gizmos ?", &parent->settings.showDDGIGizmos);

        auto probeGizmoPass = Engine::Core::GetEngine().GetRenderer()->GetRenderPass("ProbeGizmoPass");
        if (probeGizmoPass)
            probeGizmoPass->enabled = parent->settings.showDDGIGizmos;

        ImGui::Checkbox("Show Physics Shapes ?", &parent->settings.showPhysicsShapes);

        ImGui::Separator();

        ImGui::Checkbox("Show Lighting ?", &parent->settings.showLighting);
        ImGui::Checkbox("Show Shadows ?", &parent->settings.showShadows);

        namespace Rendering = Engine::Rendering;
        static const struct { const char* label; Rendering::ViewMode mode; } viewModes[] = {
            { "Lit", Rendering::ViewMode::Lit },
            { "Unlit", Rendering::ViewMode::Unlit },
            { "Wireframe", Rendering::ViewMode::Wireframe },
            { "Shaded + Wireframe", Rendering::ViewMode::ShadedWireframe },
            { "Normals", Rendering::ViewMode::Normals },
            { "Depth", Rendering::ViewMode::Depth },
            { "UVs", Rendering::ViewMode::UVs },
        };

        const char* currentLabel = "Lit";
        for (auto& vm : viewModes)
            if (vm.mode == parent->settings.viewMode)
                currentLabel = vm.label;

        if (ImGui::BeginCombo("View Mode", currentLabel))
        {
            for (auto& vm : viewModes)
            {
                const bool selected = (vm.mode == parent->settings.viewMode);
                if (ImGui::Selectable(vm.label, selected))
                    parent->settings.viewMode = vm.mode;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        auto physicsDebugPass = Engine::Core::GetEngine().GetRenderer()->GetRenderPass("PhysicsDebugPass");
        if (physicsDebugPass)
            physicsDebugPass->enabled = parent->settings.showPhysicsShapes;

        ImGui::End();
    }

    void ViewportWindow::DrawSnapControls()
    {
        static const float locationSteps[] = { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 50.0f, 100.0f };
        static const float rotationSteps[] = { 5.0f, 10.0f, 15.0f, 30.0f, 45.0f, 60.0f, 90.0f, 120.0f };
        static const float scaleSteps[]    = { 0.03125f, 0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 5.0f, 10.0f };

        // Snap toggle + increment dropdown for the tool currently selected in the toolbar. Each
        // mode keeps its own toggle/increment (see EditorSettings), so switching tool swaps them.
        auto& s = parent->settings;
        bool* enabled = nullptr;
        float* value = nullptr;
        const float* steps = nullptr;
        int stepCount = 0;
        const char* suffix = "";
        const char* tooltip = "";

        switch (currentGizmoOp)
        {
            case ImGuizmo::TRANSLATE:
                enabled = &s.snapLocation; value = &s.locationSnap;
                steps = locationSteps; stepCount = IM_ARRAYSIZE(locationSteps);
                tooltip = "Location snap";
                break;
            case ImGuizmo::ROTATE:
                enabled = &s.snapRotation; value = &s.rotationSnap;
                steps = rotationSteps; stepCount = IM_ARRAYSIZE(rotationSteps);
                suffix = "\xC2\xB0";
                tooltip = "Rotation snap";
                break;
            case ImGuizmo::SCALE:
                enabled = &s.snapScale; value = &s.scaleSnap;
                steps = scaleSteps; stepCount = IM_ARRAYSIZE(scaleSteps);
                tooltip = "Scale snap";
                break;
            default:
                return;
        }

        ImGui::PushID("GizmoSnap");

        // Latch the state before the click: the button flips *enabled, and the pop must match the push.
        const bool wasEnabled = *enabled;
        if (wasEnabled) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(ICON_LC_MAGNET)) *enabled = !*enabled;
        if (wasEnabled) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%s)", tooltip, *enabled ? "on" : "off");

        ImGui::SameLine(0.0f, 2.0f);

        char preview[32];
        snprintf(preview, sizeof(preview), "%g%s", *value, suffix);

        // Dimmed while snapping is off, but still editable so the increment can be picked first.
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (*enabled ? 1.0f : 0.6f));
        ImGui::SetNextItemWidth(72.0f);
        if (ImGui::BeginCombo("##SnapValue", preview))
        {
            for (int i = 0; i < stepCount; i++)
            {
                char item[32];
                snprintf(item, sizeof(item), "%g%s", steps[i], suffix);
                const bool selected = (*value == steps[i]);
                if (ImGui::Selectable(item, selected))
                    *value = steps[i];
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();

        ImGui::PopID();
    }

    void ViewportWindow::ShowRaytraceSettings()
    {
        namespace Raytracing = Engine::Rendering::Raytracing;

        ImGui::Begin("Offline Raytrace", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);

        if (raytracer && raytracer->IsPreparing())
        {
            // Scene/BVH build hasn't started yet at this point - it runs on the very next Update() call
            // (see Raytracer::BuildScene()'s comment), after this frame has been presented. No progress
            // fraction is meaningful yet, so just show that something is about to happen.
            ImGui::TextWrapped("%s", raytracer->GetStatusMessage().c_str());
            ImGui::TextDisabled("This can take a few seconds on large scenes.");

            if (ImGui::Button(ICON_LC_X " Cancel"))
            {
                raytracer->Cancel();
                raytraceStatusMessage = raytracer->GetStatusMessage();
                raytracer.reset();
            }
        }
        else if (raytracer && raytracer->IsRendering())
        {
            // Progress advances every frame via Draw() calling raytracer->Update() - this window just
            // reflects that state, it doesn't drive the render itself.
            ImGui::ProgressBar(raytracer->GetProgress());
            ImGui::TextWrapped("%s", raytracer->GetStatusMessage().c_str());

            if (ImGui::Button(ICON_LC_X " Cancel"))
            {
                raytracer->Cancel();
                raytraceStatusMessage = raytracer->GetStatusMessage();
                raytracer.reset();
            }
        }
        else
        {
            if (raytracer)
            {
                // Reached a terminal state (Completed/Failed) since the last time this window was open -
                // capture the final message and drop the instance (it already released its GPU buffers).
                raytraceStatusMessage = raytracer->GetStatusMessage();
                raytracer.reset();
            }

            int width = (int)raytraceSettings.width;
            if (ImGui::InputInt("Width", &width))
                raytraceSettings.width = (uint32_t)std::max(1, width);

            int height = (int)raytraceSettings.height;
            if (ImGui::InputInt("Height", &height))
                raytraceSettings.height = (uint32_t)std::max(1, height);

            int samples = (int)raytraceSettings.samplesPerPixel;
            if (ImGui::InputInt("Samples per pixel", &samples))
                raytraceSettings.samplesPerPixel = (uint32_t)std::max(1, samples);

            int bounces = (int)raytraceSettings.maxBounces;
            if (ImGui::InputInt("Max bounces", &bounces))
                raytraceSettings.maxBounces = (uint32_t)std::max(1, bounces);

            ImGui::ColorEdit3("Sky color", &raytraceSettings.skyColor.x);

            ImGui::Separator();

            ImGui::InputText("Output file", raytraceOutputPath, sizeof(raytraceOutputPath));
            ImGui::TextDisabled("Relative to the project's resources folder. Saved as linear HDR (.hdr).");

            ImGui::Separator();

            auto activeLevel = Engine::Core::GetEngine().GetLevelManager()->GetLevelAt(0);
            auto activeCamera = Engine::Core::GetEngine().GetCameraManager()->GetActiveCamera();
            bool canRender = activeLevel && activeCamera;

            ImGui::BeginDisabled(!canRender);
            if (ImGui::Button(ICON_LC_APERTURE " Render"))
            {
                std::string filename = raytraceOutputPath[0] != '\0' ? raytraceOutputPath : "render.hdr";
                auto outputPath = Engine::Filesystem::Path(
                    Engine::Core::GetEngine().GetFileManager()->GetProjectRoot().full + "/" + filename, true);

                raytraceStatusMessage.clear();

                auto newRaytracer = std::make_unique<Raytracing::Raytracer>();
                if (newRaytracer->Start(activeLevel, activeCamera, raytraceSettings, outputPath))
                    raytracer = std::move(newRaytracer);
                else
                    raytraceStatusMessage = newRaytracer->GetStatusMessage();
            }
            ImGui::EndDisabled();

            if (!canRender)
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Need a loaded level and an active camera.");
        }

        if (!raytraceStatusMessage.empty())
            ImGui::TextWrapped("%s", raytraceStatusMessage.c_str());

        ImGui::End();
    }

    void ViewportWindow::ProcessInputs()
    {
        Engine::Core::Platform::IInput* input = Engine::Core::GetEngine().GetInputManager();

        bool isDragging = !firstClick;

        if (!viewportFocused && isDragging) {
            input->SetCursorVisibility(Engine::Core::Platform::CursorVisibility::Visible);
            firstClick = true;
            isDragging = false;
        }

        bool canStartInteraction = cameraActor && viewportHovered && !uiHovered && !ImGuizmo::IsUsing() && !isUsingViewGizmo;

        if (!cameraActor || (!isDragging && !canStartInteraction))
            return;

        Engine::Time::TimeManager* time = Engine::Core::GetEngine().GetTimeManager();

        if(input->IsKeyDown(Engine::Input::Key::W)){
            cameraActor->transform->Translate(cameraActor->transform->GetForward() * speed * time->GetDeltaTime());
        }
        if(input->IsKeyDown(Engine::Input::Key::A)){
            cameraActor->transform->Translate(glm::normalize(glm::cross(cameraActor->transform->GetForward(), cameraActor->transform->GetUp())) * -speed * time->GetDeltaTime());
        }
        if(input->IsKeyDown(Engine::Input::Key::S)){
            cameraActor->transform->Translate(cameraActor->transform->GetForward() * -speed * time->GetDeltaTime());
        }
        if(input->IsKeyDown(Engine::Input::Key::D)){
            cameraActor->transform->Translate(glm::normalize(glm::cross(cameraActor->transform->GetForward(), cameraActor->transform->GetUp())) * speed * time->GetDeltaTime());
        }
        
        if (input->IsMouseDown(Engine::Input::MouseButton::Left))
        {
            if (firstClick)
            {
                input->SetCursorVisibility(Engine::Core::Platform::CursorVisibility::Disabled);
                input->GetCursorPos(&lockedMouseX, &lockedMouseY);
                firstClickTime = Engine::Core::GetEngine().GetTimeManager()->CurrentAppTime().seconds;
                firstClick = false;
            }

            double mouseX, mouseY;
            input->GetCursorPos(&mouseX, &mouseY);

            double deltaX = mouseX - lockedMouseX;
            double deltaY = lockedMouseY - mouseY;

            lockedMouseX = mouseX;
            lockedMouseY = mouseY;

            pitch += deltaY * mouseSensitivity;
            yaw   -= deltaX * mouseSensitivity;

            // Clamp pitch to avoid flipping
            pitch = glm::clamp(pitch, -90.0f, 90.0f);

            // Build quaternion from yaw * pitch
            glm::quat qPitch = glm::angleAxis(glm::radians(pitch), glm::vec3(1, 0, 0));
            glm::quat qYaw   = glm::angleAxis(glm::radians(yaw),   glm::vec3(0, 1, 0));
            glm::quat rotation = qYaw * qPitch;
            
            cameraActor->transform->SetRotation(rotation);
        }
        if(input->IsMouseUp(Engine::Input::MouseButton::Left))
        {
            if (!firstClick)
                input->SetCursorVisibility(Engine::Core::Platform::CursorVisibility::Visible);
            firstClick = true;
        }
    
        // Picking
        if(input->WasMouseReleased(Engine::Input::MouseButton::Left)){
            
            if(Engine::Core::GetEngine().GetTimeManager()->CurrentAppTime().seconds - firstClickTime > 0.3f){
                return;
            }

            Engine::Core::IEngineContext* engine = &Engine::Core::GetEngine();

            ImVec2 mouse = ImGui::GetMousePos();

            glm::vec2 max = camera->GetSize();
            max.x += viewportImageMin.x;
            max.y += viewportImageMin.y;

            if (mouse.x >= viewportImageMin.x && mouse.x <= max.x &&
                mouse.y >= viewportImageMin.y && mouse.y <= max.y)
            {
                float localX = mouse.x - viewportImageMin.x;
                float localY = mouse.y - viewportImageMin.y;

                glm::vec3 nearPoint = camera->ScreenToWorld(localX, localY, 0.0f);
                glm::vec3 farPoint  = camera->ScreenToWorld(localX, localY, 1.0f);

                glm::vec3 dir = glm::normalize(farPoint - nearPoint);
                glm::vec3 origin = nearPoint;

                Engine::Physics::RaycastResult result = Engine::Core::GetEngine().GetPhysicsManager()->RayCast({origin, dir, 10000.0f});

                std::shared_ptr<Engine::Objects::Actor> pickedActor = nullptr;
                float pickedDistance = result.hit ? result.hitDistance : 10000.0f;

                if (result.hit)
                    pickedActor = result.hitBody->parent;

                // Physics-less actors (a bare Model with no PhysicsBody) aren't visible to the
                // raycast above at all, so fall back to a CPU ray-vs-mesh-AABB test for them.
                // Actors that do have a PhysicsBody are skipped here - the raycast already tests
                // their real collider shape, which is more accurate than their mesh bounds.
                if (auto* level = engine->GetLevelManager()->GetLevelAt(0))
                {
                    for (auto& modelEntry : level->models)
                    {
                        auto& modelComp = modelEntry.second;

                        if (!modelComp || !modelComp->parent)
                            continue;

                        if (modelComp->parent->HasComponent<Engine::Objects::Components::PhysicsBody>())
                            continue;

                        auto mesh = modelComp->GetMesh();
                        if (!mesh)
                            continue;

                        glm::vec3 boundsMin = mesh->GetBoundsMin();
                        glm::vec3 boundsMax = mesh->GetBoundsMax();
                        if (boundsMin.x > boundsMax.x)
                            continue;

                        glm::mat4 worldMatrix = modelComp->parent->transform->GetWorldMatrix();

                        glm::vec3 worldMin, worldMax;
                        ComputeWorldBounds(boundsMin, boundsMax, worldMatrix, worldMin, worldMax);

                        // If the camera is inside this actor's bounds, the slab test's tMin
                        // clamps to 0 and it would spuriously win as the "closest" hit no matter
                        // where the click landed - skip it instead (e.g. camera inside a room
                        // shell or other large enclosing mesh).
                        bool cameraInside =
                            origin.x >= worldMin.x && origin.x <= worldMax.x &&
                            origin.y >= worldMin.y && origin.y <= worldMax.y &&
                            origin.z >= worldMin.z && origin.z <= worldMax.z;
                        if (cameraInside)
                            continue;

                        float t;
                        if (RayIntersectsAABB(origin, dir, worldMin, worldMax, pickedDistance, t) && t < pickedDistance)
                        {
                            pickedDistance = t;
                            pickedActor = modelComp->parent;
                        }
                    }
                }

                parent->SetSelectedActor(pickedActor);
            }
        }

    }
    
    ViewportWindow::ViewportWindow() {}

    ViewportWindow::~ViewportWindow() = default;
}