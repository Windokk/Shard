#pragma once


#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/world/components/component.hpp"
#include "engine/core/math/reference_frame.hpp"

#include "engine/assets/reflection/attributes.hpp"

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

            ordered_json Serialize() override;
            void Deserialize(const json componentData) override;

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

            void NotifyChanged(uint8_t changes);

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

            glm::mat4 GetWorldMatrix();

            glm::vec3 GetWorldPosition();
            glm::quat GetWorldRotationQuat();

            glm::vec3 GetWorldScale();

            void MarkWorldMatrixDirty();

            void SyncEntity();

            Core::FrameId GetFrame();

            const Core::ReferenceFrameTree* GetFrameTree() const;

            Core::WorldPosition GetWorldCoords();

            glm::dvec3 GetWorldPositionD();

            glm::dquat GetWorldRotationD();

            void SetWorldPositionD(const glm::dvec3& rootPosition, bool updateDirty = true);
            void SetWorldCoords(const Core::WorldPosition& rootPosition, bool updateDirty = true);

            void PlaceAt(const Core::WorldPosition& rootPosition, bool updateDirty = true);

            glm::vec3 RootToFramePosition(const glm::dvec3& rootPosition);
            glm::quat RootToFrameRotation(const glm::dquat& rootRotation);

            bool operator !=(Transform const& b) const {
                return position != b.position || rotation != b.rotation || scale != b.scale;
            }

            bool SetFromTransformMatrix(const glm::mat4 &m);

            bool SetFromWorldMatrix(const glm::mat4 &worldMatrix);

            void Destroy() override{
                //TODO ?
            }
            std::shared_ptr<Component> Clone() const override;

            bool IsDirty(DirtyFlags flag) const;
            void ClearDirty(DirtyFlags flag);

            FIELD(Editable, tooltip=Position relative to the parent actor)
            glm::vec3 position = glm::vec3(0);

            FIELD(Editable, tooltip=Rotation relative to the parent actor) 
            glm::quat rotation = glm::quat(glm::vec3(0, 0, 0));

            FIELD(Editable, tooltip=Scale relative to the parent actor) 
            glm::vec3 scale = glm::vec3(1);

            uint32_t frame = 0;

        private:

            std::shared_ptr<Transform> GetParentTransform() const;

            DirtyFlags dirtyFlags = DirtyFlags::None;

            glm::mat4 cachedWorldMatrix = glm::mat4(1.0f);
            bool worldMatrixDirty = true;

            DECLARE_DESCRIPTOR(Transform)
    };
}