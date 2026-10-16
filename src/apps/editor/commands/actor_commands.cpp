#include "actor_commands.hpp"

#include "apps/editor/commands/command_stack.hpp"

#include "engine/world/engine.hpp"
#include "engine/world/world_manager.hpp"

namespace Shard::Editor::Commands{

    using Engine::Objects::Actor;

    namespace {

        Engine::Worlds::World* CurrentWorld()
        {
            return Engine::Core::GetEngine().GetWorldManager()->GetWorldAt(0);
        }

        std::shared_ptr<Actor> ParentOf(const std::shared_ptr<Actor>& actor)
        {
            return actor ? std::dynamic_pointer_cast<Actor>(actor->GetParent()) : nullptr;
        }

        std::vector<std::shared_ptr<Actor>> ChildrenOf(const std::shared_ptr<Actor>& actor)
        {
            std::vector<std::shared_ptr<Actor>> children;
            for (Engine::Core::ObjectID childID : actor->GetChildrenID(false))
                if (auto child = std::dynamic_pointer_cast<Actor>(actor->GetChild(childID)))
                    children.push_back(child);
            return children;
        }

        // Same order as SerializeActor / InstantiateActor build it in
        void CollectParts(const std::shared_ptr<Actor>& actor, std::vector<ActorState::Part>& parts)
        {
            parts.push_back({ ObjectRefs::Get(actor), true });

            for (const auto& component : actor->GetComponents())
                parts.push_back({ ObjectRefs::Get(component), false });

            for (const auto& child : ChildrenOf(actor))
                CollectParts(child, parts);
        }

        // Points the refs saved with a state to what was just built from it. A component that could not be built
        // again (its type is gone) leaves its ref, and the ones after it on the same actor, on nothing.
        void BindParts(const std::shared_ptr<Actor>& actor, const std::vector<ActorState::Part>& parts, size_t& index)
        {
            if (index >= parts.size() || !parts[index].isActor)
                return;

            ObjectRefs::Rebind(parts[index++].ref, actor);

            const auto& components = actor->GetComponents();
            size_t componentIndex = 0;
            for (; index < parts.size() && !parts[index].isActor; ++index, ++componentIndex)
                if (componentIndex < components.size())
                    ObjectRefs::Rebind(parts[index].ref, components[componentIndex]);

            for (const auto& child : ChildrenOf(actor))
                BindParts(child, parts, index);
        }
    }

    ActorState ActorState::Capture(const std::shared_ptr<Actor>& actor)
    {
        ActorState state;
        if (!actor)
            return state;

        state.exists = true;
        state.data = Engine::Worlds::SerializeActorToJson(actor);
        state.parent = ObjectRefs::Get(ParentOf(actor));
        CollectParts(actor, state.parts);
        return state;
    }

    ActorSnapshotCommand::ActorSnapshotCommand(std::string label, const std::shared_ptr<Actor>& actor, bool existedBefore)
        : name(std::move(label)), root(ObjectRefs::Get(actor))
    {
        if (existedBefore)
            before = ActorState::Capture(actor);
    }

    void ActorSnapshotCommand::Finalize()
    {
        if (finalized)
            return;

        after = ActorState::Capture(root ? root->Resolve<Actor>() : nullptr);
        finalized = true;
    }

    bool ActorSnapshotCommand::IsValid() const
    {
        if (before.exists != after.exists)
            return true;
        if (!before.exists)
            return false;
        return before.parent != after.parent || before.data != after.data;
    }

    void ActorSnapshotCommand::Apply(const ActorState& state)
    {
        Engine::Worlds::World* world = CurrentWorld();
        if (!world)
            return;

        if (auto live = root ? root->Resolve<Actor>() : nullptr)
            live->Destroy();

        if (!state.exists)
            return;

        // Under its parent again ; the parent is only missing if the history was broken elsewhere, the root of the
        // world is the next best place
        std::shared_ptr<Actor> parent = state.parent ? state.parent->Resolve<Actor>() : nullptr;

        std::shared_ptr<Actor> actor = Engine::Worlds::InstantiateActor(*world, state.data, parent);
        if (!actor)
            return;

        size_t index = 0;
        BindParts(actor, state.parts, index);
    }

    ReparentActorCommand::ReparentActorCommand(const std::shared_ptr<Actor>& actor, const std::shared_ptr<Actor>& oldParent, const std::shared_ptr<Actor>& newParent)
        : actor(ObjectRefs::Get(actor)), oldParent(ObjectRefs::Get(oldParent)), newParent(ObjectRefs::Get(newParent))
    {
    }

    void ReparentActorCommand::Apply(const std::shared_ptr<ObjectRef>& parent)
    {
        auto a = actor ? actor->Resolve<Actor>() : nullptr;
        if (!a)
            return;

        std::shared_ptr<Actor> p = parent ? parent->Resolve<Actor>() : nullptr;
        if (parent && !p)
            return;

        a->SetParent(p);
    }

    namespace Actions {

        void MarkWorldDirty()
        {
            if (auto* world = CurrentWorld())
                world->SetDirty(true);
        }

        void RecordCreated(const std::shared_ptr<Actor>& actor, const std::string& label)
        {
            if (!actor)
                return;

            CommandStack::Get().Record(std::make_unique<ActorSnapshotCommand>(label, actor, false));
            MarkWorldDirty();
        }

        void Delete(const std::shared_ptr<Actor>& actor, const std::string& label)
        {
            if (!actor)
                return;

            auto& stack = CommandStack::Get();
            stack.Begin(label);
            stack.Add(std::make_unique<ActorSnapshotCommand>(label, actor, true));
            actor->Destroy();
            stack.End();

            MarkWorldDirty();
        }

        std::shared_ptr<Actor> Duplicate(const std::shared_ptr<Actor>& actor)
        {
            Engine::Worlds::World* world = CurrentWorld();
            if (!actor || !world)
                return nullptr;

            std::shared_ptr<Actor> copy = Engine::Worlds::InstantiateActor(*world, Engine::Worlds::SerializeActorToJson(actor), ParentOf(actor));
            if (!copy)
                return nullptr;

            copy->SetName("Copy of " + actor->GetName());
            RecordCreated(copy, "Duplicate Actor");
            return copy;
        }

        bool Reparent(const std::shared_ptr<Actor>& actor, const std::shared_ptr<Actor>& newParent)
        {
            if (!actor || actor == newParent)
                return false;

            std::shared_ptr<Actor> oldParent = ParentOf(actor);
            if (oldParent == newParent)
                return false;

            // Cycle detection and world-transform preservation ("attach in place") live in Actor::SetParent
            actor->SetParent(newParent);
            if (ParentOf(actor) != newParent)
                return false;

            CommandStack::Get().Record(std::make_unique<ReparentActorCommand>(actor, oldParent, newParent));
            MarkWorldDirty();
            return true;
        }

        void Rename(const std::shared_ptr<Actor>& actor, const std::string& name)
        {
            if (!actor || actor->GetName() == name)
                return;

            std::string oldName = actor->GetName();
            actor->SetName(name);

            CommandStack::Get().Record(std::make_unique<RenameActorCommand>(actor, oldName, name));
            MarkWorldDirty();
        }

        void SetComponentActive(const std::shared_ptr<Component>& component, bool active)
        {
            if (!component || component->Active() == active)
                return;

            active ? component->Activate() : component->DeActivate();

            CommandStack::Get().Record(std::make_unique<SetComponentActiveCommand>(component, active));
            MarkWorldDirty();
        }

        void EditActor(const std::shared_ptr<Actor>& actor, const std::string& label, const std::function<void()>& edit)
        {
            if (!actor)
                return;

            auto& stack = CommandStack::Get();
            stack.Begin(label);
            stack.Add(std::make_unique<ActorSnapshotCommand>(label, actor, true));
            edit();
            stack.End();

            MarkWorldDirty();
        }
    }

    void ActorClipboard::Copy(const std::shared_ptr<Actor>& actor)
    {
        if (!actor)
            return;

        data = Engine::Worlds::SerializeActorToJson(actor);
        worldMatrix = actor->transform ? actor->transform->GetWorldMatrix() : glm::mat4(1.0f);
        parent = ObjectRefs::Get(ParentOf(actor));
    }

    void ActorClipboard::Cut(const std::shared_ptr<Actor>& actor)
    {
        if (!actor)
            return;

        Copy(actor);
        Actions::Delete(actor, "Cut Actor");
    }

    std::shared_ptr<Actor> ActorClipboard::Paste()
    {
        Engine::Worlds::World* world = CurrentWorld();
        if (IsEmpty() || !world)
            return nullptr;

        std::shared_ptr<Actor> parentActor = parent ? parent->Resolve<Actor>() : nullptr;

        std::shared_ptr<Actor> actor = Engine::Worlds::InstantiateActor(*world, data, parentActor);
        if (!actor)
            return nullptr;

        // Its transform is relative to the parent it was copied from, which may not be the one it lands under
        if (actor->transform)
            actor->transform->SetFromWorldMatrix(worldMatrix);

        Actions::RecordCreated(actor, "Paste Actor");
        return actor;
    }
}
