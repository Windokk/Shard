#include "viewport_window.hpp"
#include "apps/editor/gui/plugin_ui.hpp"
#include "engine/c_abi_render.hpp"

#include "engine/world/engine.hpp"
#include "engine/platform/windowing/iplatform.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/world/components/transform.reflection.hpp"

#include "apps/editor/gui/IconsLucide.h"
#include "apps/editor/gui/main_window.hpp"
#include "apps/editor/gui/panels/common.hpp"
#include "apps/editor/gui/imoguizmo.hpp"
#include "apps/editor/gui/dragdrop/asset_drag_drop.hpp"

#include "engine/renderer/components/model_component.hpp"
#include "engine/renderer/components/render_world_data.hpp"
#include "engine/renderer/components/camera_manager.hpp"
#include "engine/renderer/components/camera.hpp"
#include "engine/renderer/features/billboards/billboard_renderer.hpp"
#include "engine/physics/physics_body.hpp"


#include <glm/gtx/string_cast.hpp>

#include <algorithm>
#include <cfloat>
#include <cstdio>

#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Body/BodyFilter.h>

#include <unordered_set>

#include "engine/core/diagnostics/profiler.hpp"
#include "engine/world/time_manager.hpp"

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

    // The actor's ID plus every descendant's - focus and floor-snap treat a hierarchy as one object.
    std::unordered_set<uint32_t> CollectHierarchyIDs(const std::shared_ptr<Shard::Engine::Objects::Actor>& actor)
    {
        std::unordered_set<uint32_t> ids;
        ids.insert(actor->GetID().GetAsInt());
        for (const auto& childID : actor->GetChildrenID(true))
            ids.insert(childID.GetAsInt());
        return ids;
    }

    // Merged world AABB of every model in the hierarchy. Returns false when none of them has a mesh
    // (empty actor, light, ...), in which case callers fall back to the actor's world position.
    bool ComputeHierarchyBounds(Shard::Engine::Worlds::World* world, const std::unordered_set<uint32_t>& ids,
                                glm::vec3& outMin, glm::vec3& outMax)
    {
        outMin = glm::vec3(FLT_MAX);
        outMax = glm::vec3(-FLT_MAX);
        bool found = false;

        for (auto& modelEntry : world->Ext<Shard::Engine::Rendering::RenderWorldData>().models)
        {
            auto& modelComp = modelEntry.second;
            if (!modelComp || !modelComp->parent || !ids.count(modelComp->parent->GetID().GetAsInt()))
                continue;

            auto mesh = modelComp->GetMesh();
            if (!mesh || mesh->GetBoundsMin().x > mesh->GetBoundsMax().x)
                continue;

            glm::vec3 worldMin, worldMax;
            ComputeWorldBounds(mesh->GetBoundsMin(), mesh->GetBoundsMax(),
                               modelComp->parent->transform->GetWorldMatrix(), worldMin, worldMax);
            outMin = glm::min(outMin, worldMin);
            outMax = glm::max(outMax, worldMax);
            found = true;
        }

        return found;
    }

}

namespace Shard::Editor::GUI {

    ImVec2 ViewportWindow::prev_size = ImVec2(0, 0);

    void ViewportWindow::Draw()
    {
        // The editor fly-camera must own the viewport whenever we're not in play mode. Loading or
        // reloading a world rebinds CameraManager's active camera through World::OnLoad() - to a
        // world camera, or to nothing at all when the scene has no camera actor, which left the
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

        // The fly camera's lens is the user's (Editor Preferences > Viewport) ; the orthographic size stays the camera's own
        if (camera && parent && !Engine::Core::GetEngine().IsInPlayMode())
        {
            const Core::CameraPreferences& prefs = parent->preferences.camera;
            camera->SetNearFarPlanes(prefs.nearPlane, std::max(prefs.farPlane, prefs.nearPlane + 0.01f));
            camera->SetFOV(prefs.fieldOfView);
        }

        // Dispatches one accumulated sample (or, on the first call after Start(), builds the scene/BVH)
        // per editor frame while a raytrace is active (see Raytracer::Update()'s comments) - called
        // unconditionally, regardless of whether the raytrace settings window is currently open, so the
        // render keeps progressing in the background even if the user closes that panel.
        if (raytracer && raytracer->IsActive())
            raytracer->Update();

        // Glide towards the point picked by FocusSelected() - frame-rate independent exponential ease
        if (focusAnimating && cameraActor && !Engine::Core::GetEngine().IsInPlayMode())
        {
            float dt = Engine::Core::GetEngine().GetTimeManager()->GetDeltaTime();
            const float rate = parent ? parent->preferences.camera.focusSpeed : 15.0f;
            glm::vec3 pos = cameraActor->transform->GetPosition();
            // A rate of 0 jumps straight there
            pos = rate > 0.0f ? glm::mix(pos, focusTarget, 1.0f - std::exp(-rate * dt)) : focusTarget;

            // Relative tolerance : far from the origin a float step is far bigger than a millimetre and the glide would never end
            const float tolerance = std::max(0.001f, glm::length(focusTarget) * 1.0e-6f);
            if (!(glm::distance(pos, focusTarget) >= tolerance))
            {
                pos = focusTarget;
                focusAnimating = false;
            }
            cameraActor->transform->SetPosition(pos);
        }

        const bool gameView = parent && parent->IsGameView();

        if (gameView)
        {
            // The scene alone over the whole window : no title bar, toolbar or padding. A window of its own, so that the docked
            // "Viewport" keeps its place for the way back.
            const ImGuiViewport* host = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(host->Pos);
            ImGui::SetNextWindowSize(host->Size);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::Begin("Game View", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::PopStyleVar(2);
        }
        else
        {
            ImGui::Begin("Viewport");

            float toolbarHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2;
            ImGui::BeginChild("ToolbarArea", ImVec2(0, toolbarHeight), false, ImGuiWindowFlags_NoScrollbar);
            DrawToolbar();
            ImGui::EndChild();
        }

        // Get current size
        ImVec2 current_size = ImGui::GetWindowSize();

        // Rendu texture framebuffer
        uint32_t textureID = Engine::Core::GetEngine().GetRenderer()->GetViewportTextureHandle();

        ImVec2 avail = ImGui::GetContentRegionAvail();
        viewportSize = glm::vec2(avail.x, avail.y);

        // The toolbar is not in the game view : the image is taller even when the window is not
        if ((current_size.x != prev_size.x || current_size.y != prev_size.y || gameView != gameViewShown)
            && avail.x > 0 && avail.y > 0)
        {
            Engine::Core::GetEngine().GetRenderer()->RescaleFramebuffers(avail.x, avail.y);
        }

        // Save for next frame
        prev_size = current_size;
        gameViewShown = gameView;

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

        // The overlays of plugins draw in this image : their UI maps the window positions of the input events into it
        {
            const ImVec2 windowPos = ImGui::GetMainViewport()->Pos;
            Engine::Plugins::CAbi::SetPluginOverlayOrigin(viewportImageMin.x - windowPos.x, viewportImageMin.y - windowPos.y);
        }

        // The game view only looks : nothing to drop, and no key may edit what is not shown (Delete on a hidden selection)
        if (!gameView)
        {
            HandleAssetDrop();

            HandleShortcuts();
        }

        if(!gameView && parent && parent->GetSelectedActor() && parent->settings.showGizmos)
            DrawObjectGizmo();

        // What plugins draw for the selected actors
        if(!gameView && parent && parent->HasSelection() && parent->settings.showGizmos && camera)
        {
            const float low[2] = { viewportImageMin.x, viewportImageMin.y }, high[2] = { viewportImageMax.x, viewportImageMax.y };
            DrawPluginGizmos(camera->GetProjection() * camera->GetView(), low, high, parent->GetSelectedActors());
        }

        if(marqueeActive)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 low(std::min(marqueeStart.x, marqueeEnd.x), std::min(marqueeStart.y, marqueeEnd.y));
            const ImVec2 high(std::max(marqueeStart.x, marqueeEnd.x), std::max(marqueeStart.y, marqueeEnd.y));
            drawList->PushClipRect(viewportImageMin, viewportImageMax, true);
            drawList->AddRectFilled(low, high, IM_COL32(90, 150, 255, 40));
            drawList->AddRect(low, high, IM_COL32(120, 175, 255, 220));
            drawList->PopClipRect();
        }

        viewportPos = ImGui::GetWindowPos();

        ImGui::SameLine();

        if(!gameView && parent->settings.showGizmos)
            DrawViewGizmo();
        else
            isUsingViewGizmo = false;

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

        // What plugins added : with the left-hand controls, since the right-hand group below sits against the edge and would push them out of view
        DrawPluginToolbar();

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
                parent->RequestPlay();
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

        // Not in the frame the camera is in : the two sets of numbers do not share an origin, there is nothing sensible to draw or
        // drag (F brings the camera into the object's frame)
        if (cameraActor && cameraActor->transform && selected->transform->GetFrame() != cameraActor->transform->GetFrame())
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
        const glm::mat4 transformBefore = transform;

        // Far from the origin the translations of the view and of the object are huge floats that nearly cancel, and the gizmo
        // jumps around. Hand ImGuizmo a camera at the origin and the object relative to it (the subtraction in double, then
        // the camera's position is added back to what comes out).
        glm::dvec3 gizmoOffset(0.0);
        if (cameraActor && cameraActor->transform)
        {
            gizmoOffset = glm::dvec3(cameraActor->transform->GetWorldPosition());
            view[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            transform[3] = glm::vec4(glm::vec3(glm::dvec3(transform[3]) - gizmoOffset), transform[3].w);
        }

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

            // The selected actors that move with the gizmo : the ones that have no selected ancestor (they carry their
            // children), in the frame the gizmo is in (the others have no common coordinates with it)
            gizmoTargets.clear();
            for (const auto& actor : Commands::Actions::TopLevel(parent->GetSelectedActors()))
            {
                if (!actor->transform || actor->transform->GetFrame() != selected->transform->GetFrame())
                    continue;
                gizmoTargets.push_back({ actor, actor->transform->GetWorldMatrix() });
            }

            // Where the gizmo was when the drag began (relative to the camera, like the matrix ImGuizmo works on)
            gizmoStartMatrix = transformBefore;
            gizmoStartMatrix[3] = glm::vec4(glm::vec3(glm::dvec3(gizmoStartMatrix[3]) - gizmoOffset), gizmoStartMatrix[3].w);

            stack.Begin(gizmoTargets.size() > 1 ? "Transform Objects" : "Transform Object");

            for (const auto& target : gizmoTargets)
            {
                FieldInfo* positionField = target.actor->transform->GetDescriptor()->fields[0];
                FieldInfo* rotationField = target.actor->transform->GetDescriptor()->fields[1];
                FieldInfo* scaleField    = target.actor->transform->GetDescriptor()->fields[2];

                stack.Add(std::make_unique<Commands::ModifyFieldCommand>(target.actor->transform, positionField));
                stack.Add(std::make_unique<Commands::ModifyFieldCommand>(target.actor->transform, rotationField));
                stack.Add(std::make_unique<Commands::ModifyFieldCommand>(target.actor->transform, scaleField));
            }
        }

        if (ImGuizmo::IsUsing()) {
            const bool onlyPrimary = gizmoTargets.size() == 1 && gizmoTargets[0].actor == selected;

            if (onlyPrimary)
            {
                transform[3] = glm::vec4(glm::vec3(glm::dvec3(transform[3]) + gizmoOffset), transform[3].w);
                selected->transform->SetFromWorldMatrix(transform);
            }
            else
            {
                // What the gizmo did since the drag began, applied to every selected actor from where it was (they turn and
                // scale around the gizmo, as a group)
                const glm::mat4 delta = transform * glm::inverse(gizmoStartMatrix);

                for (const auto& target : gizmoTargets)
                {
                    glm::mat4 relative = target.startWorld;
                    relative[3] = glm::vec4(glm::vec3(glm::dvec3(relative[3]) - gizmoOffset), relative[3].w);

                    glm::mat4 moved = delta * relative;
                    moved[3] = glm::vec4(glm::vec3(glm::dvec3(moved[3]) + gizmoOffset), moved[3].w);
                    target.actor->transform->SetFromWorldMatrix(moved);
                }
            }
        }

        if (!ImGuizmo::IsUsing() && gizmoActive) {
            Commands::CommandStack::Get().End();
            gizmoActive = false;
            gizmoTargets.clear();

            if (auto* world = Engine::Core::GetEngine().GetWorldManager()->GetWorldAt(0))
                world->SetDirty(true);
        }
    }

    void ViewportWindow::FocusSelected()
    {
        auto selected = parent ? parent->GetSelectedActor() : nullptr;
        auto* world = Engine::Core::GetEngine().GetWorldManager()->GetWorldAt(0);
        if (!selected || !world || !cameraActor)
            return;

        bool jumpToTarget = false;

        // Far from the origin a float cannot say where in a metre it is (at 1e30 its step is 1e23 m) : the camera could not stand next
        // to the object, it would land inside it. A root actor is placed in a frame near it (automatic : see Transform::PlaceAt), so that
        // its numbers, and the camera's, are small. Its place in the world does not change.
        if (!std::dynamic_pointer_cast<Engine::Objects::Actor>(selected->GetParent()))
        {
            const Engine::Core::WorldPosition where = selected->transform->GetWorldCoords();
            selected->transform->PlaceAt(where);
            world->SetDirty(true);
        }

        // The camera works in the frame of what it looks at, keeping where it is in the world
        {
            const Engine::Core::FrameId objectFrame = selected->transform->GetFrame();
            if (cameraActor->transform->GetFrame() != objectFrame)
            {
                const Engine::Core::WorldPosition where = cameraActor->transform->GetWorldCoords();
                cameraActor->transform->frame = objectFrame;
                cameraActor->transform->SetWorldCoords(where);
                // Where it was means nothing in the new frame when that is far from it : it jumps to the target instead of gliding
                const glm::vec3 now = cameraActor->transform->GetPosition();
                jumpToTarget = !std::isfinite(now.x) || glm::length(now) > 1.0e6f;
            }
        }

        // Everything that is selected, each actor with its descendants
        std::unordered_set<uint32_t> focusIds;
        glm::vec3 center(0.0f);
        int focusCount = 0;
        for (const auto& actor : Commands::Actions::TopLevel(parent->GetSelectedActors()))
        {
            if (!actor->transform)
                continue;
            const auto hierarchy = CollectHierarchyIDs(actor);
            focusIds.insert(hierarchy.begin(), hierarchy.end());
            center += actor->transform->GetWorldPosition();
            ++focusCount;
        }
        center = focusCount > 0 ? center / static_cast<float>(focusCount) : selected->transform->GetWorldPosition();

        glm::vec3 boundsMin, boundsMax;
        float radius = 1.0f;

        if (ComputeHierarchyBounds(world, focusIds, boundsMin, boundsMax))
        {
            center = (boundsMin + boundsMax) * 0.5f;
            radius = std::max(glm::length(boundsMax - boundsMin) * 0.5f, 0.1f);
        }

        // Back off along the current view direction (rotation is kept) until the bounding sphere fits
        // the narrowest of the two FOVs, plus a small margin.
        float distance;
        if (camera->IsOrthographic())
        {
            *camera->GetOrthoSize() = radius * 1.1f;
            distance = radius * 2.0f;
        }
        else
        {
            float aspect = viewportSize.y > 0.0f ? viewportSize.x / viewportSize.y : 1.0f;
            float halfV = glm::radians(*camera->GetFOV()) * 0.5f;
            float halfH = std::atan(std::tan(halfV) * aspect);
            distance = radius / std::sin(std::min(halfV, halfH)) * 1.1f;
        }

        const glm::vec3 target = center - cameraActor->transform->GetForward() * distance;
        // Bounds / positions that overflowed (an actor placed near FLT_MAX) give an infinity or a NaN : moving the camera there
        // would leave it, and the view with it, unusable for good
        if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z) || !std::isfinite(radius))
            return;

        focusTarget = target;
        focusAnimating = !jumpToTarget;
        if (jumpToTarget)
            cameraActor->transform->SetPosition(target);
    }

    void ViewportWindow::SnapSelectedToFloor()
    {
        auto selected = parent ? parent->GetSelectedActor() : nullptr;
        auto* world = Engine::Core::GetEngine().GetWorldManager()->GetWorldAt(0);
        if (!selected || !world)
            return;

        const auto actors = Commands::Actions::TopLevel(parent->GetSelectedActors());

        // None of the selected hierarchies counts as "the floor" for another one (nor for itself)
        std::unordered_set<uint32_t> allIds;
        for (const auto& actor : actors)
        {
            const auto hierarchy = CollectHierarchyIDs(actor);
            allIds.insert(hierarchy.begin(), hierarchy.end());
        }

        JPH::IgnoreMultipleBodiesFilter ignoreSelf;
        auto* idManager = Engine::Core::GetEngine().GetObjectIDManager();
        for (uint32_t id : allIds)
        {
            auto actor = std::dynamic_pointer_cast<Engine::Objects::Actor>(idManager->GetObjectFromID(Engine::Core::ObjectID(id)));
            if (!actor)
                continue;
            for (auto& body : actor->GetComponents<Engine::Objects::Components::PhysicsBody>())
                if (!body->GetBodyID().IsInvalid())
                    ignoreSelf.IgnoreBody(body->GetBodyID());
        }

        // Start a hair above the bottom so an object already resting on the floor still finds it
        const float startOffset = 0.01f;
        const float maxDistance = 10000.0f;

        auto& stack = Commands::CommandStack::Get();
        stack.Begin("Snap To Floor");

        for (const auto& actor : actors)
        {
            if (!actor->transform)
                continue;

            auto ids = CollectHierarchyIDs(actor);

            glm::vec3 boundsMin, boundsMax;
            glm::vec3 origin = actor->transform->GetWorldPosition();
            if (ComputeHierarchyBounds(world, ids, boundsMin, boundsMax))
                origin = glm::vec3((boundsMin.x + boundsMax.x) * 0.5f, boundsMin.y, (boundsMin.z + boundsMax.z) * 0.5f);

            origin.y += startOffset;

            Engine::Physics::RaycastRequest request{origin, glm::vec3(0, -1, 0), maxDistance};
            request.bodyFilter = &ignoreSelf;
            Engine::Physics::RaycastResult hit = Engine::Core::GetEngine().GetPhysicsManager()->RayCast(request);
            float floorDistance = hit.hit ? hit.hitDistance : FLT_MAX;

            // Same fallback as picking : physics-less models are only known by their mesh bounds,
            // so the ray "lands" on the top face of any AABB lying straight underneath.
            for (auto& modelEntry : world->Ext<Engine::Rendering::RenderWorldData>().models)
            {
                auto& modelComp = modelEntry.second;
                if (!modelComp || !modelComp->parent || allIds.count(modelComp->parent->GetID().GetAsInt()))
                    continue;
                if (modelComp->parent->HasComponent<Engine::Objects::Components::PhysicsBody>())
                    continue;

                auto mesh = modelComp->GetMesh();
                if (!mesh || mesh->GetBoundsMin().x > mesh->GetBoundsMax().x)
                    continue;

                glm::vec3 worldMin, worldMax;
                ComputeWorldBounds(mesh->GetBoundsMin(), mesh->GetBoundsMax(),
                                   modelComp->parent->transform->GetWorldMatrix(), worldMin, worldMax);

                bool underneath = origin.x >= worldMin.x && origin.x <= worldMax.x &&
                                  origin.z >= worldMin.z && origin.z <= worldMax.z &&
                                  worldMax.y <= origin.y;
                if (underneath)
                    floorDistance = std::min(floorDistance, origin.y - worldMax.y);
            }

            if (floorDistance > maxDistance)
                continue;

            float drop = floorDistance - startOffset;
            if (std::abs(drop) < 1e-5f)
                continue;

            stack.Add(std::make_unique<Commands::ModifyFieldCommand>(
                actor->transform, actor->transform->GetDescriptor()->fields[0]));

            glm::mat4 worldMatrix = actor->transform->GetWorldMatrix();
            worldMatrix[3].y -= drop;
            actor->transform->SetFromWorldMatrix(worldMatrix);
        }

        stack.End();
        world->SetDirty(true);
    }

    void ViewportWindow::HandleAssetDrop()
    {
        if (!ImGui::BeginDragDropTarget())
            return;

        std::vector<std::string> dropped = DragDrop::AcceptAssetDragDropPayload();
        if (!dropped.empty())
        {
            auto world = Engine::Core::GetEngine().GetWorldManager()->GetWorldAt(0);
            if (world)
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

                auto& history = Commands::CommandStack::Get();
                history.Begin("Place Actors");

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

                    world->AddActor(actor);
                    actor->transform->SetPosition(spawnPos);

                    auto model = actor->AddComponent<Engine::Objects::Components::Model>();
                    if (model)
                        model->SetMesh(nameInProject);

                    lastSpawned = actor;
                    Commands::Actions::RecordCreated(actor, "Place Actor");
                }

                history.End();

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
        ImGui::Text("RAM Usage: %.1f MB", stats.ramUsageMB);
        ImGui::Text("GPU Memory: %.1f MB", stats.gpuMemoryMB);

        ImGui::Separator();

        // World stats
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

        // The same values as Editor Preferences > Viewport : kept for the next sessions
        Core::CameraPreferences& prefs = parent->preferences.camera;

        // With the ranges of the preferences themselves (FIELD range=), like Editor Preferences and the file
        const Core::PreferenceRange speedRange = PREFERENCE_RANGE(Core::CameraPreferences, speed);
        const Core::PreferenceRange sensitivityRange = PREFERENCE_RANGE(Core::CameraPreferences, mouseSensitivity);
        const Core::PreferenceRange nearRange = PREFERENCE_RANGE(Core::CameraPreferences, nearPlane);
        const Core::PreferenceRange farRange = PREFERENCE_RANGE(Core::CameraPreferences, farPlane);
        const Core::PreferenceRange fovRange = PREFERENCE_RANGE(Core::CameraPreferences, fieldOfView);

        ImGui::DragFloat("Camera speed", &prefs.speed, 0.1f, speedRange.min, speedRange.max, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Mouse sensitivity", &prefs.mouseSensitivity, 0.002f, sensitivityRange.min, sensitivityRange.max, "%.3f", ImGuiSliderFlags_AlwaysClamp);

        ImGui::Separator();

        ImGui::DragFloat("Near plane", &prefs.nearPlane, 0.005f, nearRange.min, nearRange.max, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Far plane", &prefs.farPlane, 1.0f, farRange.min, farRange.max, "%.0f", ImGuiSliderFlags_AlwaysClamp);
        prefs.farPlane = std::max(prefs.farPlane, prefs.nearPlane + 0.01f);

        ImGui::Checkbox("Ortho ?", &cam->orthographic);

        if(cam->IsOrthographic()){
            ImGui::SliderFloat("Ortho size", cam->GetOrthoSize(), 1.0f, 100.0f);
        }
        else{
            ImGui::SliderFloat("FOV", &prefs.fieldOfView, fovRange.min, fovRange.max, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
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

        ImGui::Checkbox("Show Physics Shapes ?", &parent->settings.showPhysicsShapes);

        ImGui::Checkbox("Show Billboards ?", &parent->settings.showBillboards);

        ImGui::Separator();

        ImGui::Checkbox("Show Grid ?", &parent->settings.showGrid);
        if (parent->settings.showGrid)
        {
            ImGui::Indent();
            ImGui::Checkbox("X axis", &parent->settings.showGridX);
            ImGui::SameLine();
            ImGui::Checkbox("Y axis", &parent->settings.showGridY);
            ImGui::SameLine();
            ImGui::Checkbox("Z axis", &parent->settings.showGridZ);
            ImGui::Unindent();
        }

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
            { "Global Illumination", Rendering::ViewMode::GlobalIllumination },
            { "SSAO", Rendering::ViewMode::SSAO },
            { "Lightmap UVs", Rendering::ViewMode::LightmapUVs },
            { "Shadow Cascades", Rendering::ViewMode::ShadowCascades },
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

            ImGui::Checkbox("Use the world's sky", &raytraceSettings.useLevelSky);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Rays that leave the scene see the world's skybox (what the viewport draws).\nWithout one, or when this is off, they see the flat colour below.");

            ImGui::BeginDisabled(raytraceSettings.useLevelSky);
            ImGui::ColorEdit3("Sky color", &raytraceSettings.skyColor.x);
            ImGui::EndDisabled();

            ImGui::DragFloat("Firefly clamp", &raytraceSettings.fireflyClamp, 0.5f, 0.0f, 1000.0f, raytraceSettings.fireflyClamp > 0.0f ? "%.1f" : "off");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Limits the brightness one sample may carry, so a rare very bright path doesn't leave a white speck.\n0 = off.");

            ImGui::Separator();

            ImGui::InputText("Output file", raytraceOutputPath, sizeof(raytraceOutputPath));
            ImGui::TextDisabled("Relative to the project's resources folder. Saved as linear HDR (.hdr).");

            ImGui::Separator();

            // The render reads the snapshot of the last frame, like the viewport it renders
            const Engine::Rendering::RenderSnapshot& scene = Engine::Core::GetEngine().GetRenderContext()->GetSnapshot();
            bool canRender = scene.camera.valid;

            ImGui::BeginDisabled(!canRender);
            if (ImGui::Button(ICON_LC_APERTURE " Render"))
            {
                std::string filename = raytraceOutputPath[0] != '\0' ? raytraceOutputPath : "render.hdr";
                auto outputPath = Engine::Filesystem::Path(
                    Engine::Core::GetEngine().GetFileManager()->GetProjectRoot().full + "/" + filename, true);

                raytraceStatusMessage.clear();

                auto newRaytracer = std::make_unique<Raytracing::Raytracer>();
                if (newRaytracer->Start(scene, scene.camera, raytraceSettings, outputPath))
                    raytracer = std::move(newRaytracer);
                else
                    raytraceStatusMessage = newRaytracer->GetStatusMessage();
            }
            ImGui::EndDisabled();

            if (!canRender)
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Need a loaded world and an active camera.");
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

        // The game view flies the camera but does not select (the outline and the gizmo are not shown)
        const bool gameView = parent && parent->IsGameView();

        // Ctrl / Shift + button : a selection rectangle, not the camera
        if (HandleMarquee(canStartInteraction && !isDragging && !gameView))
            return;

        if (!cameraActor || (!isDragging && !canStartInteraction))
            return;

        Engine::Time::TimeManager* time = Engine::Core::GetEngine().GetTimeManager();

        const Core::CameraPreferences& cameraPrefs = parent->preferences.camera;
        const float cameraSpeed = cameraPrefs.speed;

        // Any manual camera move takes over from a running focus glide
        if (input->IsKeyDown(Engine::Input::Key::W) || input->IsKeyDown(Engine::Input::Key::A) ||
            input->IsKeyDown(Engine::Input::Key::S) || input->IsKeyDown(Engine::Input::Key::D) ||
            input->IsMouseDown(Engine::Input::MouseButton::Left))
            focusAnimating = false;

        // Ctrl+key is a shortcut (Ctrl+D duplicates), not a move
        const bool moveKeys = !ImGui::GetIO().KeyCtrl;

        if(moveKeys && input->IsKeyDown(Engine::Input::Key::W)){
            cameraActor->transform->Translate(cameraActor->transform->GetForward() * cameraSpeed * time->GetDeltaTime());
        }
        if(moveKeys && input->IsKeyDown(Engine::Input::Key::A)){
            cameraActor->transform->Translate(glm::normalize(glm::cross(cameraActor->transform->GetForward(), cameraActor->transform->GetUp())) * -cameraSpeed * time->GetDeltaTime());
        }
        if(moveKeys && input->IsKeyDown(Engine::Input::Key::S)){
            cameraActor->transform->Translate(cameraActor->transform->GetForward() * -cameraSpeed * time->GetDeltaTime());
        }
        if(moveKeys && input->IsKeyDown(Engine::Input::Key::D)){
            cameraActor->transform->Translate(glm::normalize(glm::cross(cameraActor->transform->GetForward(), cameraActor->transform->GetUp())) * cameraSpeed * time->GetDeltaTime());
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

            pitch += deltaY * cameraPrefs.mouseSensitivity * (cameraPrefs.invertY ? -1.0f : 1.0f);
            yaw   -= deltaX * cameraPrefs.mouseSensitivity;

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
        if(!gameView && input->WasMouseReleased(Engine::Input::MouseButton::Left)){
            
            if(Engine::Core::GetEngine().GetTimeManager()->CurrentAppTime().seconds - firstClickTime > 0.3f){
                return;
            }

            ImVec2 mouse = ImGui::GetMousePos();

            glm::vec2 max = camera->GetSize();
            max.x += viewportImageMin.x;
            max.y += viewportImageMin.y;

            if (mouse.x >= viewportImageMin.x && mouse.x <= max.x &&
                mouse.y >= viewportImageMin.y && mouse.y <= max.y)
            {
                float localX = mouse.x - viewportImageMin.x;
                float localY = mouse.y - viewportImageMin.y;

                ApplyPick(PickActor(localX, localY));
            }
        }

    }
    

    std::shared_ptr<Engine::Objects::Actor> ViewportWindow::PickActor(float localX, float localY)
    {
        Engine::Core::IEngineContext* engine = &Engine::Core::GetEngine();

        glm::vec3 nearPoint = camera->ScreenToWorld(localX, localY, 0.0f);
        glm::vec3 farPoint  = camera->ScreenToWorld(localX, localY, 1.0f);

        glm::vec3 dir = glm::normalize(farPoint - nearPoint);
        glm::vec3 origin = nearPoint;

        // The ray is in the camera's frame. Physics works in the root frame (and its ray starts from a float) : it can only be asked
        // when the camera is there. From another frame every model, physics body or not, goes through the bounds test below.
        const Engine::Core::ReferenceFrameTree* frames = cameraActor && cameraActor->transform ? cameraActor->transform->GetFrameTree() : nullptr;
        const Engine::Core::FrameId cameraFrame = cameraActor && cameraActor->transform ? cameraActor->transform->GetFrame() : Engine::Core::kRootFrame;
        const bool cameraInRoot = cameraFrame == Engine::Core::kRootFrame;

        Engine::Physics::RaycastResult result;
        if (cameraInRoot)
            result = Engine::Core::GetEngine().GetPhysicsManager()->RayCast({origin, dir, 10000.0f});

        std::shared_ptr<Engine::Objects::Actor> pickedActor = nullptr;
        float pickedDistance = result.hit ? result.hitDistance : 10000.0f;

        if (result.hit)
            pickedActor = result.hitBody->parent;

        // Physics-less actors (a bare Model with no PhysicsBody) aren't visible to the
        // raycast above at all, so fall back to a CPU ray-vs-mesh-AABB test for them.
        // Actors that do have a PhysicsBody are skipped here - the raycast already tests
        // their real collider shape, which is more accurate than their mesh bounds.
        if (auto* world = engine->GetWorldManager()->GetWorldAt(0))
        {
            for (auto& modelEntry : world->Ext<Engine::Rendering::RenderWorldData>().models)
            {
                auto& modelComp = modelEntry.second;

                if (!modelComp || !modelComp->parent)
                    continue;

                if (cameraInRoot && modelComp->parent->HasComponent<Engine::Objects::Components::PhysicsBody>())
                    continue;

                auto mesh = modelComp->GetMesh();
                if (!mesh)
                    continue;

                glm::vec3 boundsMin = mesh->GetBoundsMin();
                glm::vec3 boundsMax = mesh->GetBoundsMax();
                if (boundsMin.x > boundsMax.x)
                    continue;

                glm::mat4 worldMatrix = modelComp->parent->transform->GetWorldMatrix();

                // The actor's frame is not the camera's : its matrix is brought into the camera's frame, where the ray is (the offset
                // between the frames is exact, so an actor that is near the camera stays precise whatever their distance from the root)
                const Engine::Core::FrameId modelFrame = modelComp->parent->transform->GetFrame();
                if (frames && modelFrame != cameraFrame)
                    worldMatrix = glm::mat4(frames->GetRelativePose(modelFrame, cameraFrame).ToMatrix()) * worldMatrix;

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

        // The icons (lights, cameras, probe volumes, billboards) : one in front of what the ray hit wins
        {
            const Engine::Rendering::BillboardPick icon = Engine::Rendering::BillboardRenderer::Pick(
                engine->GetRenderContext()->GetSnapshot(), parent->settings.showBillboards, localX, localY, camera->GetSize().x, camera->GetSize().y);

            if (icon.hit && icon.distance < pickedDistance)
            {
                auto iconActor = std::dynamic_pointer_cast<Engine::Objects::Actor>(
                    engine->GetObjectIDManager()->GetObjectFromID(Engine::Core::ObjectID(icon.objectId)));
                if (iconActor)
                    pickedActor = iconActor;
            }
        }

        return pickedActor;
    }

    void ViewportWindow::ApplyPick(const std::shared_ptr<Engine::Objects::Actor>& actor)
    {
        const ImGuiIO& io = ImGui::GetIO();

        if (io.KeyCtrl)
        {
            if (actor)
                parent->ToggleSelection(actor);
        }
        else if (io.KeyShift)
        {
            if (actor)
                parent->AddToSelection(actor);
        }
        else
        {
            parent->SetSelectedActor(actor);
        }
    }

    bool ViewportWindow::HandleMarquee(bool canStart)
    {
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 mouse = ImGui::GetMousePos();

        if (!marqueePending)
        {
            if (canStart && firstClick && (io.KeyCtrl || io.KeyShift) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                marqueePending = true;
                marqueeActive = false;
                marqueeStart = mouse;
                marqueeEnd = mouse;
                return true;
            }
            return false;
        }

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            marqueeEnd = mouse;

            const float dx = mouse.x - marqueeStart.x;
            const float dy = mouse.y - marqueeStart.y;
            if (!marqueeActive && dx * dx + dy * dy > 16.0f)
                marqueeActive = true;
            return true;
        }

        // Released : a rectangle selects what it covers, a click without moving picks what is under the cursor
        marqueePending = false;

        if (marqueeActive)
        {
            marqueeActive = false;
            SelectInRect(marqueeStart, mouse, io.KeyCtrl);
        }
        else if (camera && mouse.x >= viewportImageMin.x && mouse.x <= viewportImageMax.x &&
                 mouse.y >= viewportImageMin.y && mouse.y <= viewportImageMax.y)
        {
            ApplyPick(PickActor(mouse.x - viewportImageMin.x, mouse.y - viewportImageMin.y));
        }

        return true;
    }

    void ViewportWindow::SelectInRect(const ImVec2& a, const ImVec2& b, bool toggle)
    {
        if (!camera || !cameraActor || !cameraActor->transform)
            return;

        const ImVec2 low(std::min(a.x, b.x), std::min(a.y, b.y));
        const ImVec2 high(std::max(a.x, b.x), std::max(a.y, b.y));

        const glm::mat4 viewProjection = camera->GetProjection() * camera->GetView();
        const ImVec2 size(viewportImageMax.x - viewportImageMin.x, viewportImageMax.y - viewportImageMin.y);
        const Engine::Core::FrameId cameraFrame = cameraActor->transform->GetFrame();

        // The pixel a point of the world is at ; false if it is behind the camera
        auto project = [&](const glm::vec3& point, ImVec2& out){
            const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0f);
            if (clip.w <= 1.0e-5f)
                return false;
            out = ImVec2(viewportImageMin.x + (clip.x / clip.w * 0.5f + 0.5f) * size.x,
                         viewportImageMin.y + (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * size.y);
            return true;
        };

        std::vector<std::shared_ptr<Engine::Objects::Actor>> covered;

        for (const auto& actor : parent->GetAllActors())
        {
            // Another frame than the camera's has other coordinates : it cannot be placed on this screen
            if (!actor->transform || actor->transform->GetFrame() != cameraFrame)
                continue;

            bool hit = false;
            bool hasMesh = false;

            // What has a mesh is its box on the screen, the rest is its pivot
            for (const auto& model : actor->GetComponents<Engine::Objects::Components::Model>())
            {
                auto mesh = model->GetMesh();
                if (!mesh || mesh->GetBoundsMin().x > mesh->GetBoundsMax().x)
                    continue;

                hasMesh = true;
                const glm::mat4 world = actor->transform->GetWorldMatrix();
                ImVec2 boxLow(FLT_MAX, FLT_MAX), boxHigh(-FLT_MAX, -FLT_MAX);
                bool any = false;

                for (int corner = 0; corner < 8; ++corner)
                {
                    const glm::vec3 local(
                        (corner & 1) ? mesh->GetBoundsMax().x : mesh->GetBoundsMin().x,
                        (corner & 2) ? mesh->GetBoundsMax().y : mesh->GetBoundsMin().y,
                        (corner & 4) ? mesh->GetBoundsMax().z : mesh->GetBoundsMin().z);

                    ImVec2 pixel;
                    if (!project(glm::vec3(world * glm::vec4(local, 1.0f)), pixel))
                        continue;

                    boxLow = ImVec2(std::min(boxLow.x, pixel.x), std::min(boxLow.y, pixel.y));
                    boxHigh = ImVec2(std::max(boxHigh.x, pixel.x), std::max(boxHigh.y, pixel.y));
                    any = true;
                }

                if (any && boxLow.x <= high.x && boxHigh.x >= low.x && boxLow.y <= high.y && boxHigh.y >= low.y)
                {
                    hit = true;
                    break;
                }
            }

            if (!hasMesh)
            {
                ImVec2 pixel;
                hit = project(actor->transform->GetWorldPosition(), pixel) &&
                      pixel.x >= low.x && pixel.x <= high.x && pixel.y >= low.y && pixel.y <= high.y;
            }

            if (hit)
                covered.push_back(actor);
        }

        std::vector<std::shared_ptr<Engine::Objects::Actor>> selection = parent->GetSelectedActors();

        if (toggle)
        {
            // What was selected leaves it, what was not joins it
            std::unordered_set<int> coveredIds;
            for (const auto& actor : covered)
                coveredIds.insert(actor->GetID().GetAsInt());

            selection.erase(std::remove_if(selection.begin(), selection.end(), [&](const auto& actor){
                return coveredIds.count(actor->GetID().GetAsInt()) > 0;
            }), selection.end());

            for (const auto& actor : covered)
                if (!parent->IsSelected(actor))
                    selection.push_back(actor);
        }
        else
        {
            selection.insert(selection.end(), covered.begin(), covered.end());
        }

        parent->SetSelectedActors(std::move(selection));
    }

    void ViewportWindow::HandleShortcuts()
    {
        if (!parent || Engine::Core::GetEngine().IsInPlayMode())
            return;

        const ImGuiIO& io = ImGui::GetIO();
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || io.WantTextInput || ImGuizmo::IsUsing())
            return;

        auto pressed = [&](ImGuiKey key, bool ctrl = false){
            return io.KeyCtrl == ctrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(key, false);
        };

        const bool hasSelection = parent->HasSelection();

        if (pressed(ImGuiKey_Delete) && hasSelection)
            parent->DeleteSelection();
        else if (pressed(ImGuiKey_C, true) && hasSelection)
            parent->CopySelection();
        else if (pressed(ImGuiKey_X, true) && hasSelection)
            parent->CutSelection();
        else if (pressed(ImGuiKey_V, true) && parent->CanPaste())
            parent->PasteClipboard();
        else if (pressed(ImGuiKey_D, true) && hasSelection)
            parent->DuplicateSelection();
        else if (pressed(ImGuiKey_A, true))
            parent->SelectAll();
        else if (pressed(ImGuiKey_G, true) && hasSelection)
            parent->GroupSelection();
        else if (pressed(ImGuiKey_F2) && hasSelection)
            parent->RenameSelection();
        else if (pressed(ImGuiKey_Escape) && hasSelection)
            parent->ClearSelection();
    }

    ViewportWindow::ViewportWindow() {}

    ViewportWindow::~ViewportWindow() = default;
}