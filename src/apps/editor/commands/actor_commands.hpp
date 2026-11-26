#pragma once

#include "apps/editor/commands/commands.hpp"
#include "apps/editor/commands/object_ref.hpp"

#include "engine/world/actor.hpp"
#include "engine/world/world.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Shard::Editor::Commands{

    struct ActorState {
        bool exists = false;
        ordered_json data;
        std::shared_ptr<ObjectRef> parent;

        struct Part {
            std::shared_ptr<ObjectRef> ref;
            bool isActor;
        };
        std::vector<Part> parts;

        static ActorState Capture(const std::shared_ptr<Engine::Objects::Actor>& actor);
    };

    // Undo and redo destroy what is there and rebuild the saved state, then point the old refs (other commands' too) at the new objects.
    class ActorSnapshotCommand final : public Command {
        public:
            ActorSnapshotCommand(std::string label, const std::shared_ptr<Engine::Objects::Actor>& actor, bool existedBefore);

            void SetBefore(ActorState state) { before = std::move(state); }

            void Finalize() override;
            bool IsValid() const override;

            void Undo() override { Apply(before); }
            void Redo() override { Apply(after); }

            const std::string& GetName() const override { return name; }

        private:
            void Apply(const ActorState& state);

            std::string name;
            std::shared_ptr<ObjectRef> root;
            ActorState before;
            ActorState after;
            bool finalized = false;
    };

    class ReparentActorCommand final : public Command {
        public:
            ReparentActorCommand(const std::shared_ptr<Engine::Objects::Actor>& actor,
                                 const std::shared_ptr<Engine::Objects::Actor>& oldParent,
                                 const std::shared_ptr<Engine::Objects::Actor>& newParent);

            bool IsValid() const override { return oldParent != newParent; }

            void Undo() override { Apply(oldParent); }
            void Redo() override { Apply(newParent); }

            const std::string& GetName() const override { return name; }

        private:
            void Apply(const std::shared_ptr<ObjectRef>& parent);

            std::string name = "Reparent Actor";
            std::shared_ptr<ObjectRef> actor;
            std::shared_ptr<ObjectRef> oldParent;
            std::shared_ptr<ObjectRef> newParent;
    };

    class RenameActorCommand final : public Command {
        public:
            RenameActorCommand(const std::shared_ptr<Engine::Objects::Actor>& actor, std::string oldName, std::string newName)
                : actor(ObjectRefs::Get(actor)), oldName(std::move(oldName)), newName(std::move(newName)) {}

            bool IsValid() const override { return oldName != newName; }

            void Undo() override { Apply(oldName); }
            void Redo() override { Apply(newName); }

            const std::string& GetName() const override { return name; }

        private:
            void Apply(const std::string& value) {
                if (auto a = actor ? actor->Resolve<Engine::Objects::Actor>() : nullptr)
                    a->SetName(value);
            }

            std::string name = "Rename Actor";
            std::shared_ptr<ObjectRef> actor;
            std::string oldName;
            std::string newName;
    };

    class SetFolderCommand final : public Command {
        public:
            explicit SetFolderCommand(std::string label) : name(std::move(label)) {}

            void AddActor(const std::shared_ptr<Engine::Objects::Actor>& actor, std::string newFolder) {
                entries.push_back({ ObjectRefs::Get(actor), actor->GetFolder(), std::move(newFolder) });
            }

            bool IsValid() const override {
                for (const auto& e : entries)
                    if (e.before != e.after)
                        return true;
                return false;
            }

            void Undo() override { for (const auto& e : entries) Apply(e.actor, e.before); }
            void Redo() override { for (const auto& e : entries) Apply(e.actor, e.after); }

            const std::string& GetName() const override { return name; }

        private:
            struct Entry {
                std::shared_ptr<ObjectRef> actor;
                std::string before;
                std::string after;
            };

            static void Apply(const std::shared_ptr<ObjectRef>& ref, const std::string& folder) {
                if (auto a = ref ? ref->Resolve<Engine::Objects::Actor>() : nullptr)
                    a->SetFolder(folder);
            }

            std::string name;
            std::vector<Entry> entries;
    };

    class SetWorldFoldersCommand final : public Command {
        public:
            SetWorldFoldersCommand(std::string label, std::vector<std::string> before, std::vector<std::string> after)
                : name(std::move(label)), before(std::move(before)), after(std::move(after)) {}

            bool IsValid() const override { return before != after; }

            void Undo() override { Apply(before); }
            void Redo() override { Apply(after); }

            const std::string& GetName() const override { return name; }

        private:
            static void Apply(const std::vector<std::string>& folders);

            std::string name;
            std::vector<std::string> before;
            std::vector<std::string> after;
    };

    class SetComponentActiveCommand final : public Command {
        public:
            SetComponentActiveCommand(const std::shared_ptr<Component>& component, bool active)
                : component(ObjectRefs::Get(component)), active(active) {}

            void Undo() override { Apply(!active); }
            void Redo() override { Apply(active); }

            const std::string& GetName() const override { return name; }

        private:
            void Apply(bool value) {
                if (auto c = component ? component->Resolve<Component>() : nullptr)
                    value ? c->Activate() : c->DeActivate();
            }

            std::string name = "Toggle Component";
            std::shared_ptr<ObjectRef> component;
            bool active;
    };

    // The order of the siblings of one parent (null parent : the root actors of the world), as it is before / after an edit.
    struct SiblingOrder {
        std::shared_ptr<ObjectRef> parent;
        std::vector<std::shared_ptr<ObjectRef>> actors;

        bool operator==(const SiblingOrder& other) const { return parent == other.parent && actors == other.actors; }

        static SiblingOrder Capture(const std::shared_ptr<Engine::Objects::Actor>& parent);
    };

    // Restores sibling orders. An edit that also moves actors between parents records two of these around its other commands :
    // one that only acts on Undo (it runs last, once the actors are back where they were) and one that only acts on Redo (it runs last too, once they are moved).
    class ReorderActorsCommand final : public Command {
        public:
            // Takes the lists as they are now (before the edit), and acts on Undo.
            ReorderActorsCommand(std::string label, const std::vector<std::shared_ptr<Engine::Objects::Actor>>& parents);

            // The partner that acts on Redo : same lists to compare with, built after the edit.
            ReorderActorsCommand(const ReorderActorsCommand& first, bool actsOnUndo);

            void Finalize() override;
            bool IsValid() const override { return before != after; }

            void Undo() override { if (actsOnUndo) Apply(before); }
            void Redo() override { if (!actsOnUndo) Apply(after); }

            const std::string& GetName() const override { return name; }

        private:
            static void Apply(const std::vector<SiblingOrder>& orders);

            std::string name;
            bool actsOnUndo = true;
            std::vector<std::shared_ptr<ObjectRef>> parentRefs;
            std::vector<SiblingOrder> before;
            std::vector<SiblingOrder> after;
    };

    class ReorderComponentsCommand final : public Command {
        public:
            explicit ReorderComponentsCommand(const std::shared_ptr<Engine::Objects::Actor>& actor);

            void Finalize() override;
            bool IsValid() const override { return before != after; }

            void Undo() override { Apply(before); }
            void Redo() override { Apply(after); }

            const std::string& GetName() const override { return name; }

        private:
            static std::vector<std::shared_ptr<ObjectRef>> Capture(const std::shared_ptr<Engine::Objects::Actor>& actor);
            void Apply(const std::vector<std::shared_ptr<ObjectRef>>& order);

            std::string name = "Reorder Components";
            std::shared_ptr<ObjectRef> actor;
            std::vector<std::shared_ptr<ObjectRef>> before;
            std::vector<std::shared_ptr<ObjectRef>> after;
    };

    namespace Actions {

        // Moves the component of an actor to another place of its list (undoable). False when nothing moved (the Transform stays first).
        bool MoveComponent(const std::shared_ptr<Engine::Objects::Actor>& actor, size_t from, size_t to);

        // Puts the actors before or after `anchor`, under its parent and in its folder (one undo step). Returns how many actors were moved.
        int MoveActorsNextTo(const std::vector<std::shared_ptr<Engine::Objects::Actor>>& actors, const std::shared_ptr<Engine::Objects::Actor>& anchor, bool after);

        void RecordCreated(const std::shared_ptr<Engine::Objects::Actor>& actor, const std::string& label = "Create Actor");

        void Delete(const std::shared_ptr<Engine::Objects::Actor>& actor, const std::string& label = "Delete Actor");

        std::shared_ptr<Engine::Objects::Actor> Duplicate(const std::shared_ptr<Engine::Objects::Actor>& actor);

        bool Reparent(const std::shared_ptr<Engine::Objects::Actor>& actor, const std::shared_ptr<Engine::Objects::Actor>& newParent);

        void Rename(const std::shared_ptr<Engine::Objects::Actor>& actor, const std::string& name);

        void SetComponentActive(const std::shared_ptr<Component>& component, bool active);

        void EditActor(const std::shared_ptr<Engine::Objects::Actor>& actor, const std::string& label, const std::function<void()>& edit);

        void MarkWorldDirty();

        std::shared_ptr<Engine::Objects::Actor> PlacePrefab(const std::string& nameInProject, const glm::vec3* position = nullptr, const std::string& folder = "");

        void SetPrefabLink(const std::shared_ptr<Engine::Objects::Actor>& actor, const std::string& nameInProject);

        void UnpackPrefab(const std::shared_ptr<Engine::Objects::Actor>& root);

        std::vector<std::shared_ptr<Engine::Objects::Actor>> TopLevel(const std::vector<std::shared_ptr<Engine::Objects::Actor>>& actors);

        bool IsSelfOrAncestor(const std::shared_ptr<Engine::Objects::Actor>& ancestor, const std::shared_ptr<Engine::Objects::Actor>& actor);

        std::shared_ptr<Engine::Objects::Actor> CreateActor(const std::shared_ptr<Engine::Objects::Actor>& parent, const std::string& name = "New Actor",
                                                            const std::string& folder = "");

        void DeleteMany(const std::vector<std::shared_ptr<Engine::Objects::Actor>>& actors, const std::string& label = "Delete Actors");

        std::vector<std::shared_ptr<Engine::Objects::Actor>> DuplicateMany(const std::vector<std::shared_ptr<Engine::Objects::Actor>>& actors);

        int ReparentMany(const std::vector<std::shared_ptr<Engine::Objects::Actor>>& actors, const std::shared_ptr<Engine::Objects::Actor>& newParent);

        void MoveToFolder(const std::vector<std::shared_ptr<Engine::Objects::Actor>>& actors, const std::string& folder);

        std::shared_ptr<Engine::Objects::Actor> GroupActors(const std::vector<std::shared_ptr<Engine::Objects::Actor>>& actors, const std::string& name = "Group");

        std::string NormalizeFolderPath(const std::string& path);
        bool IsInFolder(const std::string& path, const std::string& folder);
        std::string CreateFolder(const std::string& path);
        bool RenameFolder(const std::string& path, const std::string& newPath);
        void DeleteFolder(const std::string& path, bool deleteActors);
    }

    class ActorClipboard {
        public:
            void Copy(const std::vector<std::shared_ptr<Engine::Objects::Actor>>& actors);

            void Cut(const std::vector<std::shared_ptr<Engine::Objects::Actor>>& actors);

            std::vector<std::shared_ptr<Engine::Objects::Actor>> Paste();

            bool IsEmpty() const { return entries.empty(); }

        private:
            struct Entry {
                ordered_json data;
                glm::mat4 worldMatrix = glm::mat4(1.0f);
                std::shared_ptr<ObjectRef> parent;
            };
            std::vector<Entry> entries;
    };
}
