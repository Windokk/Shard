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

            /// @brief Tells the other components of the actor that this transform changed (see Component::OnTransformChanged)
            /// @param changes TransformChange flags
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

            /// @brief Copies the local fields to the LocalTransform of the actor's entity (done by MarkWorldMatrixDirty)
            void SyncEntity();

            // ---- Large worlds. The position / rotation / scale above are small numbers measured in the actor's reference frame (see
            // Core::ReferenceFrameTree, owned by the World) ; these give the same pose in the root frame, in double.
            // The frame of a hierarchy is the one set on its root actor : children are relative to their parent whatever their own value.

            /// @brief The reference frame this actor's hierarchy lives in (kRootFrame when the world has no such frame)
            Core::FrameId GetFrame();

            /// @brief The frames of the world this actor is in (null for an actor that is in none)
            const Core::ReferenceFrameTree* GetFrameTree() const;

            /// @brief World position in the root frame, as an exact (cell + local) position
            Core::WorldPosition GetWorldCoords();

            /// @brief World position in the root frame as a plain double (exact to the double's precision at that distance)
            glm::dvec3 GetWorldPositionD();

            /// @brief World rotation in the root frame (the frame's own rotation included)
            glm::dquat GetWorldRotationD();

            /// @brief Moves the actor to a position of the root frame (converted into its frame, then into its parent's space)
            void SetWorldPositionD(const glm::dvec3& rootPosition, bool updateDirty = true);
            void SetWorldCoords(const Core::WorldPosition& rootPosition, bool updateDirty = true);

            /// @brief Like SetWorldCoords, but also picks the frame : a root actor keeps its frame while it stays near that frame's origin,
            /// else moves to a frame near `rootPosition` (an existing automatic one, or a new one). Its numbers stay small wherever it is.
            void PlaceAt(const Core::WorldPosition& rootPosition, bool updateDirty = true);

            /// @brief A point / a rotation of the root frame, turned into the actor's frame (the space its world matrix lives in)
            glm::vec3 RootToFramePosition(const glm::dvec3& rootPosition);
            glm::quat RootToFrameRotation(const glm::dquat& rootRotation);

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

            FIELD(Editable, tooltip=Position relative to the parent actor)
            glm::vec3 position = glm::vec3(0);

            FIELD(Editable, tooltip=Rotation relative to the parent actor) 
            glm::quat rotation = glm::quat(glm::vec3(0, 0, 0));

            FIELD(Editable, tooltip=Scale relative to the parent actor) 
            glm::vec3 scale = glm::vec3(1);

            /// The reference frame the position is measured in (read on the root actor of a hierarchy). Not a field : frames are managed
            /// by the engine (see PlaceAt), nobody picks one by hand. Saved with the transform.
            uint32_t frame = 0;

        private:

            std::shared_ptr<Transform> GetParentTransform() const;

            DirtyFlags dirtyFlags = DirtyFlags::None;

            glm::mat4 cachedWorldMatrix = glm::mat4(1.0f);
            bool worldMatrixDirty = true;

            DECLARE_DESCRIPTOR(Transform)
    };
}