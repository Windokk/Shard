#include "actor_commands.hpp"

#include "apps/editor/commands/command_stack.hpp"

#include "engine/world/engine.hpp"
#include "engine/world/world_manager.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <set>
#include <unordered_set>

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

    void SetWorldFoldersCommand::Apply(const std::vector<std::string>& folders)
    {
        if (auto* world = CurrentWorld())
            world->GetFolders() = folders;
    }

    namespace Actions {

        namespace {

            // Every folder the outliner shows : the ones the world lists and the ones its root actors are in
            std::set<std::string> AllFolders(Engine::Worlds::World* world)
            {
                std::set<std::string> folders;
                for (const auto& f : world->GetFolders())
                    folders.insert(f);
                for (const auto& [id, actor] : world->GetRootActors())
                    if (actor && !actor->GetFolder().empty())
                        folders.insert(actor->GetFolder());
                return folders;
            }

            std::string ParentFolder(const std::string& path)
            {
                const size_t slash = path.rfind('/');
                return slash == std::string::npos ? std::string() : path.substr(0, slash);
            }

            std::string JoinFolder(const std::string& base, const std::string& rest)
            {
                if (base.empty())
                    return rest;
                return rest.empty() ? base : base + "/" + rest;
            }

            // `path` with its leading `from` replaced by `to` (path is in the folder `from`)
            std::string ReplacePrefix(const std::string& path, const std::string& from, const std::string& to)
            {
                std::string rest = path.size() > from.size() ? path.substr(from.size() + 1) : std::string();
                return JoinFolder(to, rest);
            }
        }

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

    namespace Actions {

        std::vector<std::shared_ptr<Actor>> TopLevel(const std::vector<std::shared_ptr<Actor>>& actors)
        {
            std::unordered_set<int> listed;
            for (const auto& a : actors)
                if (a)
                    listed.insert(a->GetID().GetAsInt());

            std::vector<std::shared_ptr<Actor>> result;
            std::unordered_set<int> added;
            for (const auto& a : actors)
            {
                if (!a || !added.insert(a->GetID().GetAsInt()).second)
                    continue;

                bool carried = false;
                for (auto p = ParentOf(a); p && !carried; p = ParentOf(p))
                    carried = listed.count(p->GetID().GetAsInt()) > 0;

                if (!carried)
                    result.push_back(a);
            }
            return result;
        }

        bool IsSelfOrAncestor(const std::shared_ptr<Actor>& ancestor, const std::shared_ptr<Actor>& actor)
        {
            for (auto a = actor; a; a = ParentOf(a))
                if (a == ancestor)
                    return true;
            return false;
        }

        std::shared_ptr<Actor> CreateActor(const std::shared_ptr<Actor>& parent, const std::string& name, const std::string& folder)
        {
            Engine::Worlds::World* world = CurrentWorld();
            if (!world)
                return nullptr;

            auto actor = Engine::Core::Object::CreateWithContext<Actor>(&Engine::Core::GetEngine(), name, &Engine::Core::GetEngine());
            if (parent)
                parent->AddChild(actor);
            else
            {
                world->AddActor(actor);
                actor->SetFolder(folder);
            }

            RecordCreated(actor);
            return actor;
        }

        void DeleteMany(const std::vector<std::shared_ptr<Actor>>& actors, const std::string& label)
        {
            const auto top = TopLevel(actors);
            if (top.empty())
                return;

            auto& stack = CommandStack::Get();
            stack.Begin(label);
            for (const auto& actor : top)
                Delete(actor, label);
            stack.End();
        }

        std::vector<std::shared_ptr<Actor>> DuplicateMany(const std::vector<std::shared_ptr<Actor>>& actors)
        {
            std::vector<std::shared_ptr<Actor>> copies;

            const auto top = TopLevel(actors);
            if (top.empty())
                return copies;

            auto& stack = CommandStack::Get();
            stack.Begin("Duplicate Actors");
            for (const auto& actor : top)
                if (auto copy = Duplicate(actor))
                    copies.push_back(copy);
            stack.End();

            return copies;
        }

        int ReparentMany(const std::vector<std::shared_ptr<Actor>>& actors, const std::shared_ptr<Actor>& newParent)
        {
            int moved = 0;

            auto& stack = CommandStack::Get();
            stack.Begin("Reparent Actors");
            for (const auto& actor : TopLevel(actors))
            {
                // Under itself or its own subtree : refused
                if (newParent && IsSelfOrAncestor(actor, newParent))
                    continue;

                if (Reparent(actor, newParent))
                    ++moved;
            }
            stack.End();

            return moved;
        }

        void MoveToFolder(const std::vector<std::shared_ptr<Actor>>& actors, const std::string& folder)
        {
            const std::string target = NormalizeFolderPath(folder);
            const auto top = TopLevel(actors);
            if (top.empty())
                return;

            auto& stack = CommandStack::Get();
            stack.Begin("Move To Folder");

            // A folder holds root actors
            for (const auto& actor : top)
                if (ParentOf(actor))
                    Reparent(actor, nullptr);

            auto command = std::make_unique<SetFolderCommand>("Move To Folder");
            for (const auto& actor : top)
                command->AddActor(actor, target);
            for (const auto& actor : top)
                actor->SetFolder(target);
            stack.Record(std::move(command));

            stack.End();
            MarkWorldDirty();
        }

        std::shared_ptr<Actor> GroupActors(const std::vector<std::shared_ptr<Actor>>& actors, const std::string& name)
        {
            const auto top = TopLevel(actors);
            if (top.empty())
                return nullptr;

            auto& stack = CommandStack::Get();
            stack.Begin("Group Actors");

            // Its own step first : the snapshot of the new actor is taken when this group of commands ends, before the
            // actors are put under it (else a redo would build them a second time)
            stack.Begin("Create Group");
            const auto firstParent = ParentOf(top[0]);
            auto group = CreateActor(firstParent, name, firstParent ? std::string() : top[0]->GetFolder());
            if (group && group->transform)
            {
                glm::vec3 center(0.0f);
                for (const auto& actor : top)
                    center += actor->transform->GetWorldPosition();
                center /= static_cast<float>(top.size());
                group->transform->SetFromWorldMatrix(glm::translate(glm::mat4(1.0f), center));
            }
            stack.End();

            if (group)
                for (const auto& actor : top)
                    Reparent(actor, group);

            stack.End();
            return group;
        }

        std::string NormalizeFolderPath(const std::string& path)
        {
            std::string result;
            std::string part;
            auto flush = [&]{
                // Spaces around a part are not a name
                const size_t first = part.find_first_not_of(' ');
                if (first != std::string::npos)
                {
                    if (!result.empty())
                        result += '/';
                    result += part.substr(first, part.find_last_not_of(' ') - first + 1);
                }
                part.clear();
            };

            for (char c : path)
            {
                if (c == '/' || c == '\\')
                    flush();
                else
                    part += c;
            }
            flush();

            return result;
        }

        bool IsInFolder(const std::string& path, const std::string& folder)
        {
            if (folder.empty())
                return true;
            return path == folder || (path.size() > folder.size() && path.compare(0, folder.size(), folder) == 0 && path[folder.size()] == '/');
        }

        std::string CreateFolder(const std::string& path)
        {
            Engine::Worlds::World* world = CurrentWorld();
            std::string wanted = NormalizeFolderPath(path);
            if (!world || wanted.empty())
                return std::string();

            const auto existing = AllFolders(world);
            std::string unique = wanted;
            for (int n = 2; existing.count(unique); ++n)
                unique = wanted + " " + std::to_string(n);

            std::vector<std::string> before = world->GetFolders();
            std::vector<std::string> after = before;
            after.push_back(unique);

            world->GetFolders() = after;
            CommandStack::Get().Record(std::make_unique<SetWorldFoldersCommand>("Create Folder", std::move(before), std::move(after)));
            MarkWorldDirty();
            return unique;
        }

        bool RenameFolder(const std::string& path, const std::string& newPath)
        {
            Engine::Worlds::World* world = CurrentWorld();
            const std::string from = NormalizeFolderPath(path);
            const std::string to = NormalizeFolderPath(newPath);
            if (!world || from.empty() || to.empty() || from == to || IsInFolder(to, from))
                return false;

            // Moving a folder onto another one would merge them, which is not what a typo in a name should do
            if (AllFolders(world).count(to))
                return false;

            auto& stack = CommandStack::Get();
            stack.Begin("Rename Folder");

            auto command = std::make_unique<SetFolderCommand>("Rename Folder");
            std::vector<std::shared_ptr<Actor>> inside;
            for (const auto& [id, actor] : world->GetRootActors())
                if (actor && IsInFolder(actor->GetFolder(), from))
                    inside.push_back(actor);

            for (const auto& actor : inside)
                command->AddActor(actor, ReplacePrefix(actor->GetFolder(), from, to));
            for (const auto& actor : inside)
                actor->SetFolder(ReplacePrefix(actor->GetFolder(), from, to));
            stack.Record(std::move(command));

            std::vector<std::string> before = world->GetFolders();
            std::vector<std::string> after = before;
            for (auto& f : after)
                if (IsInFolder(f, from))
                    f = ReplacePrefix(f, from, to);
            // A folder only the actors in it made is still a folder once they are gone
            if (std::find(after.begin(), after.end(), to) == after.end())
                after.push_back(to);
            world->GetFolders() = after;
            stack.Record(std::make_unique<SetWorldFoldersCommand>("Rename Folder", std::move(before), std::move(after)));

            stack.End();
            MarkWorldDirty();
            return true;
        }

        void DeleteFolder(const std::string& path, bool deleteActors)
        {
            Engine::Worlds::World* world = CurrentWorld();
            const std::string folder = NormalizeFolderPath(path);
            if (!world || folder.empty())
                return;

            std::vector<std::shared_ptr<Actor>> inside;
            for (const auto& [id, actor] : world->GetRootActors())
                if (actor && IsInFolder(actor->GetFolder(), folder))
                    inside.push_back(actor);

            auto& stack = CommandStack::Get();
            stack.Begin("Delete Folder");

            if (deleteActors)
            {
                DeleteMany(inside, "Delete Actors");
            }
            else
            {
                // They move up to the folder around this one, with the sub folders they were in
                const std::string up = ParentFolder(folder);
                auto command = std::make_unique<SetFolderCommand>("Delete Folder");
                for (const auto& actor : inside)
                    command->AddActor(actor, ReplacePrefix(actor->GetFolder(), folder, up));
                for (const auto& actor : inside)
                    actor->SetFolder(ReplacePrefix(actor->GetFolder(), folder, up));
                stack.Record(std::move(command));
            }

            std::vector<std::string> before = world->GetFolders();
            std::vector<std::string> after;
            for (const auto& f : before)
            {
                if (f == folder)
                    continue;
                if (IsInFolder(f, folder))
                {
                    if (deleteActors)
                        continue;
                    after.push_back(ReplacePrefix(f, folder, ParentFolder(folder)));
                }
                else
                {
                    after.push_back(f);
                }
            }
            world->GetFolders() = after;
            stack.Record(std::make_unique<SetWorldFoldersCommand>("Delete Folder", std::move(before), std::move(after)));

            stack.End();
            MarkWorldDirty();
        }
    }

    void ActorClipboard::Copy(const std::vector<std::shared_ptr<Actor>>& actors)
    {
        const auto top = Actions::TopLevel(actors);
        if (top.empty())
            return;

        entries.clear();
        for (const auto& actor : top)
        {
            Entry entry;
            entry.data = Engine::Worlds::SerializeActorToJson(actor);
            entry.worldMatrix = actor->transform ? actor->transform->GetWorldMatrix() : glm::mat4(1.0f);
            entry.parent = ObjectRefs::Get(ParentOf(actor));
            entries.push_back(std::move(entry));
        }
    }

    void ActorClipboard::Cut(const std::vector<std::shared_ptr<Actor>>& actors)
    {
        if (actors.empty())
            return;

        Copy(actors);
        Actions::DeleteMany(actors, "Cut Actors");
    }

    std::vector<std::shared_ptr<Actor>> ActorClipboard::Paste()
    {
        std::vector<std::shared_ptr<Actor>> pasted;

        Engine::Worlds::World* world = CurrentWorld();
        if (IsEmpty() || !world)
            return pasted;

        auto& stack = CommandStack::Get();
        stack.Begin("Paste Actors");

        for (const auto& entry : entries)
        {
            std::shared_ptr<Actor> parentActor = entry.parent ? entry.parent->Resolve<Actor>() : nullptr;

            std::shared_ptr<Actor> actor = Engine::Worlds::InstantiateActor(*world, entry.data, parentActor);
            if (!actor)
                continue;

            // Its transform is relative to the parent it was copied from, which may not be the one it lands under
            if (actor->transform)
                actor->transform->SetFromWorldMatrix(entry.worldMatrix);

            Actions::RecordCreated(actor, "Paste Actor");
            pasted.push_back(actor);
        }

        stack.End();
        return pasted;
    }
}
