#include "transform.hpp"


#include <glm/gtx/matrix_decompose.hpp>

#include "engine/world/actor.hpp"

#include "engine/world/engine.hpp"
#include "engine/core/ecs/registry.hpp"
#include "engine/world/ecs_components.hpp"
#include "engine/world/world_manager.hpp"

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

    ordered_json Transform::Serialize()
    {
        ordered_json data = SerializeReflected("transform");
        if (frame != Core::kRootFrame)
            data["frame"] = frame;
        return data;
    }

    void Transform::Deserialize(const json componentData)
    {
        Component::Deserialize(componentData);
        frame = componentData.contains("frame") && componentData["frame"].is_number_unsigned() ? componentData["frame"].get<uint32_t>() : Core::kRootFrame;
        MarkWorldMatrixDirty();
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

    const Core::ReferenceFrameTree* Transform::GetFrameTree() const
    {
        if (!parent)
            return nullptr;
        if (parent->world)
            return &parent->world->GetFrames();

        // An actor that belongs to no world (the editor's camera) lives in the frames of the world that is being shown
        Core::IEngineContext* engine = parent->GetEngineContext();
        Worlds::WorldManager* worlds = engine ? engine->GetWorldManager() : nullptr;
        Worlds::World* shown = worlds ? worlds->GetWorldAt(0) : nullptr;
        return shown ? &shown->GetFrames() : nullptr;
    }

    Core::FrameId Transform::GetFrame()
    {
        const Core::ReferenceFrameTree* tree = GetFrameTree();
        if (!tree)
            return Core::kRootFrame;

        const Transform* root = this;
        std::shared_ptr<Transform> up = GetParentTransform();
        while (up) {
            root = up.get();
            up = root->GetParentTransform();
        }
        return tree->Exists(root->frame) ? root->frame : Core::kRootFrame;
    }

    Core::WorldPosition Transform::GetWorldCoords()
    {
        const glm::dvec3 inFrame = glm::dvec3(GetWorldPosition());
        const Core::ReferenceFrameTree* tree = GetFrameTree();
        const Core::FrameId frameId = GetFrame();
        if (!tree || frameId == Core::kRootFrame)
            return Core::WorldPosition::FromDVec3(inFrame);
        return tree->ToRoot(frameId, inFrame);
    }

    glm::dvec3 Transform::GetWorldPositionD()
    {
        return GetWorldCoords().ToDVec3();
    }

    glm::dquat Transform::GetWorldRotationD()
    {
        const glm::dquat inFrame = glm::dquat(GetWorldRotationQuat());
        const Core::ReferenceFrameTree* tree = GetFrameTree();
        const Core::FrameId frameId = GetFrame();
        if (!tree || frameId == Core::kRootFrame)
            return inFrame;
        return glm::normalize(tree->GetRotationInRoot(frameId) * inFrame);
    }

    glm::vec3 Transform::RootToFramePosition(const glm::dvec3& rootPosition)
    {
        const Core::ReferenceFrameTree* tree = GetFrameTree();
        const Core::FrameId frameId = GetFrame();
        if (!tree || frameId == Core::kRootFrame)
            return glm::vec3(rootPosition);
        return glm::vec3(tree->FromRoot(frameId, Core::WorldPosition::FromDVec3(rootPosition)));
    }

    glm::quat Transform::RootToFrameRotation(const glm::dquat& rootRotation)
    {
        const Core::ReferenceFrameTree* tree = GetFrameTree();
        const Core::FrameId frameId = GetFrame();
        if (!tree || frameId == Core::kRootFrame)
            return glm::quat(rootRotation);
        return glm::quat(glm::normalize(glm::inverse(tree->GetRotationInRoot(frameId)) * rootRotation));
    }

    void Transform::SetWorldCoords(const Core::WorldPosition& rootPosition, bool updateDirty)
    {
        const Core::ReferenceFrameTree* tree = GetFrameTree();
        const Core::FrameId frameId = GetFrame();

        // The point in the frame : converted from the exact root position, so a far position keeps what the frame can hold
        glm::vec3 inFrame;
        if (!tree || frameId == Core::kRootFrame)
            inFrame = glm::vec3(rootPosition.ToDVec3());
        else
            inFrame = glm::vec3(tree->FromRoot(frameId, rootPosition));

        std::shared_ptr<Transform> parentTransform = GetParentTransform();
        if (parentTransform)
            inFrame = glm::vec3(glm::inverse(parentTransform->GetWorldMatrix()) * glm::vec4(inFrame, 1.0f));

        SetPosition(inFrame, updateDirty);
    }

    void Transform::PlaceAt(const Core::WorldPosition& rootPosition, bool updateDirty)
    {
        Core::ReferenceFrameTree* tree = const_cast<Core::ReferenceFrameTree*>(GetFrameTree());
        if (tree && !GetParentTransform()) {
            // A float is exact to a millimetre within ~10 km : an actor farther than this from its frame's origin gets a nearer one
            constexpr double kReach = 8192.0;
            const auto near = [&](Core::FrameId id) {
                const glm::dvec3 d = Core::WorldPosition::Offset(rootPosition, tree->GetOriginInRoot(id));
                return std::abs(d.x) < kReach && std::abs(d.y) < kReach && std::abs(d.z) < kReach;
            };

            Core::FrameId chosen = Core::kInvalidFrame;
            if (tree->Exists(frame) && near(frame))
                chosen = frame;
            else if (near(Core::kRootFrame))
                chosen = Core::kRootFrame;
            else {
                for (const Core::ReferenceFrame& f : tree->GetFrames())
                    if (f.id != Core::kRootFrame && f.name.rfind("Auto ", 0) == 0 && near(f.id)) {
                        chosen = f.id;
                        break;
                    }
            }
            if (chosen == Core::kInvalidFrame) {
                const Core::WorldPosition origin = rootPosition.InCellRange() ? Core::WorldPosition(rootPosition.cell, glm::dvec3(0.0)) : rootPosition;
                chosen = tree->Create("Auto " + std::to_string(tree->Count()), Core::kRootFrame, origin);
            }
            if (chosen != Core::kInvalidFrame)
                frame = chosen;
        }
        SetWorldCoords(rootPosition, updateDirty);
    }

    void Transform::SetWorldPositionD(const glm::dvec3& rootPosition, bool updateDirty)
    {
        SetWorldCoords(Core::WorldPosition::FromDVec3(rootPosition), updateDirty);
    }

    void Transform::SyncEntity()
    {
        if (!parent)
            return;
        Core::IEngineContext* engine = parent->GetEngineContext();
        Core::Ecs::Registry* ecs = engine ? engine->GetEcs() : nullptr;
        if (!ecs)
            return;

        const Worlds::LocalTransform local{position, rotation, scale};
        const Core::Ecs::Entity entity = parent->GetEntity();
        if (Worlds::LocalTransform* mirror = ecs->TryGet<Worlds::LocalTransform>(entity))
            *mirror = local;
        else
            ecs->Add(entity, local);
    }

    void Transform::MarkWorldMatrixDirty()
    {
        worldMatrixDirty = true;
        SyncEntity();

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

		glm::vec3 Row[3];

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