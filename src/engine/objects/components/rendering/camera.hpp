#pragma once

#include "engine/rendering/utils.hpp"

#include "engine/objects/components/core/component.hpp"

#include "engine/core/attributes.hpp"

namespace Shard::Engine::Objects::Components {
    
    struct Plane
    {
        glm::vec3 normal = { 0.f, 1.f, 0.f }; // unit vector
        float     d = 0.f;        // Distance with origin

        Plane() = default;

        Plane(const glm::vec3& p1, const glm::vec3& norm)
            : normal(glm::normalize(norm)),
            d(glm::dot(normal, p1))
        {}

        float getSignedDistanceToPlane(const glm::vec3& point) const
        {
            return glm::dot(normal, point) - d;
        }
    };

    struct Frustum
    {
        Plane topFace;
        Plane bottomFace;

        Plane rightFace;
        Plane leftFace;

        Plane farFace;
        Plane nearFace;
    };

    class CLASS() Camera : public Component
    {
        public :
            
            Camera(std::shared_ptr<Actor> parent, uint32_t local_id);

            void Init(int width, int height, float near, float far, float fov, bool ortho, float orthoSize);

            void AddToCameraManager();

            void Activate() override;
            void DeActivate() override;

            void Destroy() override;

            void UpdateSize(int new_width, int new_height);

            void UpdateMatrix();

            void SetOrthographic(bool ortho) { orthographic = ortho; }

            bool IsOrthographic() { return orthographic; }

            void SetFOV(float newFOV) { fov = newFOV; }

            float* GetFOV() { return &fov; }

            void SetOrthoSize(float orthoSize) { this->orthoSize = orthoSize; }

            float* GetOrthoSize() { return &orthoSize; }
 
            void OnFieldChanged(const FieldChangedEvent& event) override;

            void SetNearPlane(float newNear) { nearPlane = newNear; }
            void SetFarPlane(float newFar) { farPlane = newFar; }
            void SetNearFarPlanes(float newNear, float newFar) { nearPlane = newNear; farPlane = newFar; }

            void ToggleFrustumCulling() { frustumCulling = !frustumCulling; }

            void Deserialize(const json componentData) override;
            
            ordered_json Serialize() override;
            
            std::shared_ptr<Component> Clone() const override;

            bool IsInFrustum(const glm::vec3& boundsMin, const glm::vec3& boundsMax);

            glm::vec3 ScreenToWorld(float X, float Y, float depth);

            glm::mat4 GetMatrix()
            {
                return cameraMatrix;
            }

            glm::mat4 GetView()
            {
                return view;
            }

            glm::mat4 GetProjection()
            {
                return projection;
            }

            glm::vec2 GetSize()
            {
                return glm::vec2(width, height);
            }

            // Smallest near plane a perspective camera may use (matches the viewport's slider minimum)
            static constexpr float MIN_NEAR_PLANE = 0.01f;

            FIELD(Editable)
            float farPlane = 100.0f;

            FIELD(Editable)
            float nearPlane = 0.1f;

            FIELD(Editable)
            bool orthographic = false;

            FIELD(Editable)
            bool frustumCulling = true;

            FIELD(Editable)
            float fov = 60.0f;

            FIELD(Editable)
            float orthoSize = 10.0f;

        private:

            // A perspective projection needs 0 < near < far : with near == 0 every depth collapses onto the
            // far plane and the scene disappears, and the shadow cascade splits turn into NaN.
            void SanitizePlanes();

            // Matrices
            glm::mat4 view;
            glm::mat4 projection;
            glm::mat4 cameraMatrix = glm::mat4(1.0f);

            // Store the width and height of the cam
            int width = 0;
            int height = 0;

            DECLARE_DESCRIPTOR(Camera)
    };
}