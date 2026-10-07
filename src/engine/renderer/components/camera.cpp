#include "camera.hpp"

#include <algorithm>
#include <iostream>
#include <string>

#include "engine/world/actor.hpp"
#include "engine/renderer/components/camera_manager.hpp"

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

        glm::mat4 m = cameraMatrix;

        Frustum frustum;

        // Right plane
        frustum.rightFace.normal.x = m[0][3] - m[0][0];
        frustum.rightFace.normal.y = m[1][3] - m[1][0];
        frustum.rightFace.normal.z = m[2][3] - m[2][0];
        frustum.rightFace.d        = m[3][3] - m[3][0];

        // Left plane
        frustum.leftFace.normal.x = m[0][3] + m[0][0];
        frustum.leftFace.normal.y = m[1][3] + m[1][0];
        frustum.leftFace.normal.z = m[2][3] + m[2][0];
        frustum.leftFace.d        = m[3][3] + m[3][0];

        // Bottom plane
        frustum.bottomFace.normal.x = m[0][3] + m[0][1];
        frustum.bottomFace.normal.y = m[1][3] + m[1][1];
        frustum.bottomFace.normal.z = m[2][3] + m[2][1];
        frustum.bottomFace.d        = m[3][3] + m[3][1];

        // Top plane
        frustum.topFace.normal.x = m[0][3] - m[0][1];
        frustum.topFace.normal.y = m[1][3] - m[1][1];
        frustum.topFace.normal.z = m[2][3] - m[2][1];
        frustum.topFace.d        = m[3][3] - m[3][1];

        // Far plane
        frustum.farFace.normal.x = m[0][3] - m[0][2];
        frustum.farFace.normal.y = m[1][3] - m[1][2];
        frustum.farFace.normal.z = m[2][3] - m[2][2];
        frustum.farFace.d        = m[3][3] - m[3][2];

        // Near plane
        frustum.nearFace.normal.x = m[0][3] + m[0][2];
        frustum.nearFace.normal.y = m[1][3] + m[1][2];
        frustum.nearFace.normal.z = m[2][3] + m[2][2];
        frustum.nearFace.d        = m[3][3] + m[3][2];

        // Normalize planes
        auto normalizePlane = [](Plane &p) {
            float len = glm::length(p.normal);
            p.normal /= len;
            p.d /= len;
        };

        normalizePlane(frustum.rightFace);
        normalizePlane(frustum.leftFace);
        normalizePlane(frustum.topFace);
        normalizePlane(frustum.bottomFace);
        normalizePlane(frustum.nearFace);
        normalizePlane(frustum.farFace);

        Plane planes[6] = {
            frustum.rightFace, frustum.leftFace, frustum.topFace,
            frustum.bottomFace, frustum.nearFace, frustum.farFace
        };

        for (int i = 0; i < 6; ++i)
        {
            Plane &plane = planes[i];

            // Compute the positive vertex
            glm::vec3 pVertex = boundsMin;

            if (plane.normal.x >= 0) pVertex.x = boundsMax.x;
            if (plane.normal.y >= 0) pVertex.y = boundsMax.y;
            if (plane.normal.z >= 0) pVertex.z = boundsMax.z;

            // If the positive vertex is outside the plane, the AABB is outside
            if (glm::dot(plane.normal, pVertex) + plane.d < 0)
            {
                return false;
            }
        }

        // Otherwise, it's at least partially inside
        return true;
    }

    void Camera::OnFieldChanged(const FieldChangedEvent &event)
    {
        
    }

}