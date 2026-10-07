#include "camera.hpp"

#include <algorithm>
#include <iostream>
#include <string>

#include "engine/world/actor.hpp"
#include "engine/renderer/components/camera_manager.hpp"
#include "engine/renderer/rhi/frustum.hpp"

#include "camera.reflection.hpp"

#include "engine/platform/windowing/iplatform.hpp"

#include "engine/world/engine.hpp"
#include <glm/gtx/string_cast.hpp>

namespace Shard::Engine::Objects::Components {
    
    Camera::Camera(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {
    }

    void Camera::Init(int width, int height, float near, float far, float fov, bool ortho, float orthoSize)
    {
        this->width = width;
        this->height = height;
        this->nearPlane = near;
        this->farPlane = far;
        this->fov = fov;
        this->orthographic = ortho;
        this->orthoSize = orthoSize;

        SanitizePlanes();
    }

    void Camera::SanitizePlanes()
    {
        if (!orthographic)
            nearPlane = std::max(nearPlane, MIN_NEAR_PLANE);

        farPlane = std::max(farPlane, nearPlane + MIN_NEAR_PLANE);
    }

    void Camera::Destroy()
    {
        if(parent)
            GetEngineContext()->GetCameraManager()->RemoveCamera(parent->GetID());
    }

    void Camera::AddToCameraManager()
    {
        if(parent)
            GetEngineContext()->GetCameraManager()->AddCamera(parent->GetID(), std::static_pointer_cast<Camera>(shared_from_this()));
    }

    void Camera::Activate()
    {
        Component::Activate();

        if(parent)
            GetEngineContext()->GetCameraManager()->PromoteNextActiveCamera();
    }

    void Camera::DeActivate()
    {
        Component::DeActivate();

        if(parent)
            GetEngineContext()->GetCameraManager()->PromoteNextActiveCamera();
    }

    void Camera::UpdateSize(int new_width, int new_height)
    {
        if(!activated)
            return;

        this->width = new_width;
        this->height = new_height;
    }

    void Camera::UpdateMatrix()
    {
        if (!activated || parent == nullptr || parent->transform == nullptr)
            return;

        // The planes are editable fields, so they can be changed (or left unset) without going through Init()
        SanitizePlanes();

        // Reset matrices
        view = glm::mat4(1.0f);
        projection = glm::mat4(1.0f);

        std::shared_ptr<Transform> tr = parent->transform;

        glm::vec3 position = tr->GetWorldPosition();
        glm::vec3 forward  = tr->GetWorldForward();
        glm::vec3 up       = tr->GetWorldUp();

        // View matrix
        glm::mat4 rot = glm::mat4_cast(tr->GetWorldRotationQuat());
        glm::mat4 trans = glm::translate(glm::mat4(1.0f), position);
        view = glm::inverse(trans * rot);

        // Avoid division by zero
        float aspect = (height != 0) ? float(width) / float(height) : 1.0f;

        if (orthographic)
        {
            // Orthographic projection
            float right = orthoSize * aspect;
            float left  = -right;
            float top   = orthoSize;
            float bottom= -top;

            projection = glm::ortho(left, right, bottom, top, nearPlane, farPlane);
        }
        else
        {
            // Perspective projection
            projection = glm::perspective(
                glm::radians(fov),
                aspect,
                nearPlane,
                farPlane
            );
        }

        // Final camera matrix
        cameraMatrix = projection * view;
    }

    glm::vec3 Camera::ScreenToWorld(float X, float Y, float depth)
    {
        float x = (2.0f * X) / width - 1.0f;
        float y = 1.0f - (2.0f * Y) / height;
        float z = 2.0f * depth - 1.0f;

        glm::vec4 clip(x, y, z, 1.0f);

        glm::mat4 invVP = glm::inverse(projection * view);

        glm::vec4 world = invVP * clip;
        world /= world.w;

        return glm::vec3(world);
    }

    void Camera::Deserialize(const json componentData)
    {
        json data = componentData;

        // Levels saved before the serialization went through the reflection call the planes "near" and "far"
        if (data.is_object())
        {
            if (!data.contains("nearPlane") && data.contains("near"))
                data["nearPlane"] = data["near"];

            if (!data.contains("farPlane") && data.contains("far"))
                data["farPlane"] = data["far"];
        }

        DeserializeReflectedFields(data);

        if (!orthographic && nearPlane < MIN_NEAR_PLANE)
            DEBUG_WARNING("Camera near plane " + std::to_string(nearPlane) + " is too small for a perspective camera, using " + std::to_string(MIN_NEAR_PLANE));

        // The fields are in, the camera has to size its projection from the window and sanitize the planes
        Init(GetEngineContext()->GetWindow()->GetFramebufferWidth(), GetEngineContext()->GetWindow()->GetFramebufferHeight(),
             nearPlane, farPlane, fov, orthographic, orthoSize);

        DeserializeActive(data, false);
    }

    ordered_json Camera::Serialize()
    {
        return SerializeReflected("camera");
    }

    std::shared_ptr<Component> Camera::Clone() const
    {
        auto cloned = Object::Create<Camera>(*this);

        return cloned;
    }

    bool Camera::IsInFrustum(const glm::vec3 &boundsMin, const glm::vec3 &boundsMax)
    {
        if (!frustumCulling)
            return true;

        return Rendering::AABBInFrustum(Rendering::ExtractFrustumPlanes(cameraMatrix), boundsMin, boundsMax);
    }

    void Camera::OnFieldChanged(const FieldChangedEvent &event)
    {
        
    }

}