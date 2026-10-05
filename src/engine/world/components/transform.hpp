#pragma once


#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/world/components/component.hpp"

#include "engine/core/attributes.hpp"

namespace Shard::Engine::Objects::Components
{
    enum class DirtyFlags : uint8_t
    {
        None     = 0,
        Position = 1 << 0,
        Rotation = 1 << 1,
        Scale    = 1 << 2,
        All      = Position | Rotation | Scale
    };

    inline DirtyFlags operator|(DirtyFlags a, DirtyFlags b)
    {
        return static_cast<DirtyFlags>(
            static_cast<uint8_t>(a) | static_cast<uint8_t>(b)
        );
    }

    inline DirtyFlags& operator|=(DirtyFlags& a, DirtyFlags b)
    {
        a = a | b;
        return a;
    }

    inline bool operator&(DirtyFlags a, DirtyFlags b)
    {
        return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
    }

    class CLASS() Transform : public Component{

        public:
            Transform(std::shared_ptr<Actor> parent, uint32_t local_id);

            void Deserialize(const json componentData) override;

            ordered_json Serialize() override;

            const glm::vec3& GetPosition() const { return position; }
            const glm::vec3 GetRotation() const { return glm::degrees(glm::eulerAngles(rotation)); }
            const glm::quat GetRotationQuat() const { return rotation; }
            const glm::vec3& GetScale() const { return scale; }

            void SetPosition(glm::vec3 position, bool updateDirty = true);
            void SetRotation(glm::vec3 rotation, bool updateDirty = true);
            void SetRotation(glm::quat rotation, bool updateDirty = true);
            void SetScale(glm::vec3 scale, bool updateDirty = true);

            void Translate(glm::vec3 deltaPosition, bool updateDirty = true);
            void Rotate(glm::vec3 angle, bool updateDirty = true);
            void Scale(glm::vec3 deltaScale, bool updateDirty = true);

            void UpdateMeshReferencesInLevel();

            void OnFieldChanged(const FieldChangedEvent& event) override;

            glm::vec3 GetForward(){
                return glm::normalize(rotation * glm::vec3(0, 0, -1));
            }

            glm::vec3 GetUp(){
                return glm::normalize(rotation * glm::vec3(0, 1, 0));
            }

            glm::vec3 GetRight(){
                return glm::normalize(rotation * glm::vec3(1, 0, 0));
            }

            glm::vec3 GetWorldForward() {
                return glm::normalize(GetWorldRotationQuat() * glm::vec3(0, 0, -1));
            }

            glm::vec3 GetWorldUp() {
                return glm::normalize(GetWorldRotationQuat() * glm::vec3(0, 1, 0));
            }

            glm::vec3 GetWorldRight() {
                return glm::normalize(GetWorldRotationQuat() * glm::vec3(1, 0, 0));
            }

            glm::mat4 GetTransformMatrix();

            /// @brief World-space matrix, composed as parent->GetWorldMatrix() * GetTransformMatrix().
            /// Lazily recomputed (cached, invalidated by MarkWorldMatrixDirty()) - identity-parented
            /// (root) actors just return their local matrix.
            glm::mat4 GetWorldMatrix();

            glm::vec3 GetWorldPosition();
            glm::quat GetWorldRotationQuat();

            /// @brief Lossy world-space scale (length of each world matrix basis column) - "lossy"
            /// because it can't represent shear introduced by a non-uniformly-scaled ancestor combined
            /// with rotation, same caveat as Unity's Transform.lossyScale.
            glm::vec3 GetWorldScale();

            /// @brief Marks the cached world matrix stale and propagates the same to every descendant,
            /// since a child's world matrix depends on its parent's. Called automatically whenever a
            /// local field changes or an actor is reparented (see Actor::AddChild/SetParent).
            void MarkWorldMatrixDirty();

            bool operator !=(Transform const& b) const {
                return position != b.position || rotation != b.rotation || scale != b.scale;
            }

            bool SetFromTransformMatrix(const glm::mat4 &m);

            /// @brief Like SetFromTransformMatrix, but m is expressed in world space - converted to
            /// local space against the parent's current world matrix before being applied. Used by the
            /// editor gizmo, which manipulates the world-space matrix.
            bool SetFromWorldMatrix(const glm::mat4 &worldMatrix);

            void Destroy() override{
                //TODO ?
            }
            std::shared_ptr<Component> Clone() const override;

            bool IsDirty(DirtyFlags flag) const;
            void ClearDirty(DirtyFlags flag);

            FIELD(Editable)
            glm::vec3 position = glm::vec3(0);

            FIELD(Editable) 
            glm::quat rotation = glm::quat(glm::vec3(0, 0, 0));

            FIELD(Editable) 
            glm::vec3 scale = glm::vec3(1);

        private:

            std::shared_ptr<Transform> GetParentTransform() const;

            DirtyFlags dirtyFlags = DirtyFlags::None;

            glm::mat4 cachedWorldMatrix = glm::mat4(1.0f);
            bool worldMatrixDirty = true;

            DECLARE_DESCRIPTOR(Transform)
    };
}