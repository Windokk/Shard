#include "transform.hpp"


#include <glm/gtx/matrix_decompose.hpp>

#include "engine/world/actor.hpp"

#include "engine/world/engine.hpp"

#include "transform.reflection.hpp"

#include "glm/gtx/string_cast.hpp"

namespace Shard::Engine::Objects::Components{

	bool Transform::IsDirty(DirtyFlags flag) const
	{
		return dirtyFlags & flag;
	}

	void Transform::ClearDirty(DirtyFlags flag)
	{
		dirtyFlags = static_cast<DirtyFlags>(
			static_cast<uint8_t>(dirtyFlags) &
			~static_cast<uint8_t>(flag)
		);
	}

    Transform::Transform(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {
        this->position = glm::vec3(0,0,0);
        this->rotation = glm::quat(glm::vec3(0,0,0));
        this->scale = glm::vec3(1,1,1);
    }

    void Transform::Deserialize(const json componentData)
    {
		auto safeGetVec3 = [&](const char* key) -> glm::vec3
		{
			glm::vec3 result(0.0f);

			if (!componentData.contains(key) || !componentData[key].is_object())
				return result;

			const auto& obj = componentData[key];

			auto getFloat = [&](const char* axis) -> float
			{
				if (obj.contains(axis) && obj[axis].is_number())
					return obj[axis].get<float>();
				return 0.0f;
			};

			return glm::vec3(
				getFloat("x"),
				getFloat("y"),
				getFloat("z")
			);
		};

		SetPosition(safeGetVec3("position"));
		SetRotation(safeGetVec3("rotation"));
		SetScale(safeGetVec3("scale"));
	}

    ordered_json Transform::Serialize()
    {
        ordered_json comp;

        comp["type"] = "transform";

        comp["active"] = activated;

		comp["position"]["x"] = position.x;
		comp["position"]["y"] = position.y;
		comp["position"]["z"] = position.z;

		comp["rotation"]["x"] = GetRotation().x;
		comp["rotation"]["y"] = GetRotation().y;
		comp["rotation"]["z"] = GetRotation().z;
		
		comp["scale"]["x"] = scale.x;
		comp["scale"]["y"] = scale.y;
		comp["scale"]["z"] = scale.z;

		return comp;
    }

    void Transform::SetPosition(glm::vec3 position, bool updateDirty)
    {
        if(!activated)
            return;

        this->position = position;

		// Invalidate (and propagate) the world matrix cache before anything below reads a world-space
		// value derived from it, otherwise it would read the pre-edit, now-stale cached matrix.
		MarkWorldMatrixDirty();

		NotifyChanged(TransformPosition);

		if(updateDirty)
			dirtyFlags |= DirtyFlags::Position;
    }

    void Transform::SetRotation(glm::vec3 rotation, bool updateDirty)
    {
        if(!activated)
            return;

		this->rotation = glm::quat(glm::radians(rotation));

		MarkWorldMatrixDirty();

		NotifyChanged(TransformRotation);

		if(updateDirty)
			dirtyFlags |= DirtyFlags::Rotation;
    }

    void Transform::SetRotation(glm::quat rotation, bool updateDirty)
    {
		if(!activated)
            return;

		this->rotation = rotation;

		MarkWorldMatrixDirty();

		NotifyChanged(TransformRotation);

		if(updateDirty)
			dirtyFlags |= DirtyFlags::Rotation;
    }

    void Transform::SetScale(glm::vec3 scale, bool updateDirty)
    {
        if(!activated)
            return;

        this->scale = scale;

		MarkWorldMatrixDirty();
		NotifyChanged(TransformScale);

		if(updateDirty)
			dirtyFlags |= DirtyFlags::Scale;
    }

    void Transform::Translate(glm::vec3 deltaPosition, bool updateDirty)
    {
        if(!activated)
            return;

        this->position += deltaPosition;

		MarkWorldMatrixDirty();

		NotifyChanged(TransformPosition);

		if(updateDirty)
			dirtyFlags |= DirtyFlags::Position;
    }

    void Transform::Rotate(glm::vec3 angle, bool updateDirty)
    {
        if(!activated)
            return;

        glm::vec3 radians = glm::radians(angle);

		glm::quat qPitch = glm::angleAxis(radians.x, glm::vec3(1, 0, 0));
		glm::quat qYaw   = glm::angleAxis(radians.y, glm::vec3(0, 1, 0));
		glm::quat qRoll  = glm::angleAxis(radians.z, glm::vec3(0, 0, 1));

		this->rotation = qYaw * qPitch * qRoll * this->rotation;

		MarkWorldMatrixDirty();

		NotifyChanged(TransformRotation);

		if(updateDirty)
			dirtyFlags |= DirtyFlags::Rotation;
    }

    void Transform::Scale(glm::vec3 deltaScale, bool updateDirty)
    {
        if(!activated)
            return;

        this->scale += deltaScale;

		MarkWorldMatrixDirty();
		NotifyChanged(TransformScale);

		if(updateDirty)
			dirtyFlags |= DirtyFlags::Scale;
    }

	void Transform::NotifyChanged(uint8_t changes)
	{
        if(!activated)
            return;

		// The other components of the actor (a light, a model...) follow the transform by themselves
		const auto& siblings = parent->GetComponents();
		for(size_t i = 0; i < siblings.size(); i++){
			if(siblings[i].get() != this)
				siblings[i]->OnTransformChanged(changes);
		}
	}

    glm::mat4 Transform::GetTransformMatrix()
    {
        return glm::translate(glm::mat4(1.0f), position)
            * glm::toMat4(rotation)
            * glm::scale(glm::mat4(1.0f), scale);
    }

    std::shared_ptr<Transform> Transform::GetParentTransform() const
    {
        if (!parent)
            return nullptr;

        auto parentActor = std::dynamic_pointer_cast<Actor>(parent->GetParent());
        return parentActor ? parentActor->transform : nullptr;
    }

    glm::mat4 Transform::GetWorldMatrix()
    {
        if (worldMatrixDirty)
        {
            std::shared_ptr<Transform> parentTransform = GetParentTransform();

            cachedWorldMatrix = parentTransform
                ? parentTransform->GetWorldMatrix() * GetTransformMatrix()
                : GetTransformMatrix();

            worldMatrixDirty = false;
        }

        return cachedWorldMatrix;
    }

    glm::vec3 Transform::GetWorldPosition()
    {
        return glm::vec3(GetWorldMatrix()[3]);
    }

    glm::quat Transform::GetWorldRotationQuat()
    {
        glm::mat4 m = GetWorldMatrix();

        glm::mat3 rotationMatrix(
            glm::normalize(glm::vec3(m[0])),
            glm::normalize(glm::vec3(m[1])),
            glm::normalize(glm::vec3(m[2]))
        );

        return glm::quat_cast(rotationMatrix);
    }

    glm::vec3 Transform::GetWorldScale()
    {
        glm::mat4 m = GetWorldMatrix();

        return glm::vec3(
            glm::length(glm::vec3(m[0])),
            glm::length(glm::vec3(m[1])),
            glm::length(glm::vec3(m[2]))
        );
    }

    void Transform::MarkWorldMatrixDirty()
    {
        worldMatrixDirty = true;

        if (!parent)
            return;

        for (const Core::ObjectID& childID : parent->GetChildrenID())
        {
            auto childActor = std::dynamic_pointer_cast<Actor>(
                Core::GetEngine().GetObjectIDManager()->GetObjectFromID(childID));

            if (!childActor || !childActor->transform)
                continue;

            // Recurse first so the whole subtree is marked dirty regardless of any child's previous
            // state, then eagerly refresh Model/Volume's world->meshes snapshot (see
            // NotifyChanged) - those are pushed, not pulled each frame, so a descendant
            // whose own local fields never changed still needs to be told its world matrix moved.
            childActor->transform->MarkWorldMatrixDirty();
            childActor->transform->NotifyChanged(TransformHierarchy);
        }
    }

    bool Transform::SetFromTransformMatrix(const glm::mat4& m)
    {
        if(!activated)
            return false;

		using T = float;

		glm::mat4 localMatrix(m);

		if (glm::epsilonEqual(localMatrix[3][3], static_cast<float>(0), glm::epsilon<T>()))
			return false;

		if (
			glm::epsilonNotEqual(localMatrix[0][3], static_cast<T>(0), glm::epsilon<T>()) ||
			glm::epsilonNotEqual(localMatrix[1][3], static_cast<T>(0), glm::epsilon<T>()) ||
			glm::epsilonNotEqual(localMatrix[2][3], static_cast<T>(0), glm::epsilon<T>()))
		{
			// Clear the perspective partition
			localMatrix[0][3] = localMatrix[1][3] = localMatrix[2][3] = static_cast<T>(0);
			localMatrix[3][3] = static_cast<T>(1);
		}

		if(glm::vec3(localMatrix[3]) != this->position){
			SetPosition(glm::vec3(localMatrix[3]));
		}
		
		localMatrix[3] = glm::vec4(0, 0, 0, localMatrix[3].w);

		glm::vec3 Row[3], Pdum3;

		for (glm::length_t i = 0; i < 3; ++i)
			for (glm::length_t j = 0; j < 3; ++j)
				Row[i][j] = localMatrix[i][j];

		glm::vec3 sca;

		sca.x = length(Row[0]);
		Row[0] = glm::detail::scale(Row[0], static_cast<T>(1));
		sca.y = length(Row[1]);
		Row[1] = glm::detail::scale(Row[1], static_cast<T>(1));
		sca.z = length(Row[2]);
		Row[2] = glm::detail::scale(Row[2], static_cast<T>(1));

		if(sca != this->scale){
			SetScale(sca);
		}

        glm::mat3 rotationMatrix;
		rotationMatrix[0] = Row[0];
		rotationMatrix[1] = Row[1];
		rotationMatrix[2] = Row[2];

		glm::quat q = glm::quat_cast(rotationMatrix);

		if(q != this->rotation){
			SetRotation(q);
		}
		
		
		NotifyChanged(TransformHierarchy);

		return true;
    }

    bool Transform::SetFromWorldMatrix(const glm::mat4& worldMatrix)
    {
        std::shared_ptr<Transform> parentTransform = GetParentTransform();

        glm::mat4 localMatrix = parentTransform
            ? glm::inverse(parentTransform->GetWorldMatrix()) * worldMatrix
            : worldMatrix;

        return SetFromTransformMatrix(localMatrix);
    }

    std::shared_ptr<Component> Transform::Clone() const
    {
        auto cloned = Object::Create<Transform>(*this);

        // Copy-constructed from *this, so it would otherwise inherit the source's cached world matrix -
        // stale as soon as the clone gets attached under a (potentially different) parent actor.
        cloned->worldMatrixDirty = true;

        return cloned;
    }

    void Transform::OnFieldChanged(const FieldChangedEvent &event)
    {
		if(std::string(event.field->name) == "position"){
			SetPosition(position);
		}
		else if(std::string(event.field->name) == "rotation"){
			SetRotation(rotation);
		}
		else if(std::string(event.field->name) == "scale"){
			SetScale(scale);
		}
    }
}