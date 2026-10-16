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

    /// An actor and its subtree at one point of the history : enough to build it again
    struct ActorState {
        bool exists = false;
        ordered_json data;                          // SerializeActorToJson
        std::shared_ptr<ObjectRef> parent;          // null : a root actor of the world

        // What it is made of, in the order it is built in : each actor, its components, then its children
        struct Part {
            std::shared_ptr<ObjectRef> ref;
            bool isActor;
        };
        std::vector<Part> parts;

        static ActorState Capture(const std::shared_ptr<Engine::Objects::Actor>& actor);
    };

    /// @brief Whatever happens to an actor (and its subtree) between the construction of the command and End() :
    /// created, destroyed, components added or removed... Undo and redo destroy what is there and build the saved
    /// state again ; the refs of what they replace (the other commands' too) are pointed to what they build.
    class ActorSnapshotCommand final : public Command {
        public:
            /// @param existedBefore false when the edit being recorded is the creation of `actor` (already built)
            ActorSnapshotCommand(std::string label, const std::shared_ptr<Engine::Objects::Actor>& actor, bool existedBefore);

            /// The state before the edit, when the caller noticed it after the actor had already changed
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

    /// An actor moved under another one (or to the root of the world), keeping where it is in the world
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

    /// @brief The world edits of the editor's panels : each one does the edit, records it in the undo history
    /// and marks the world dirty.
    namespace Actions {

        /// Records the creation of an actor the caller just built and set up
        void RecordCreated(const std::shared_ptr<Engine::Objects::Actor>& actor, const std::string& label = "Create Actor");

        void Delete(const std::shared_ptr<Engine::Objects::Actor>& actor, const std::string& label = "Delete Actor");

        /// A copy of the actor and its subtree, next to it
        std::shared_ptr<Engine::Objects::Actor> Duplicate(const std::shared_ptr<Engine::Objects::Actor>& actor);

        /// Moves the actor under newParent (null : the root of the world), keeping where it is in the world.
        /// Refused (false) if newParent is the actor or one of its descendants.
        bool Reparent(const std::shared_ptr<Engine::Objects::Actor>& actor, const std::shared_ptr<Engine::Objects::Actor>& newParent);

        void Rename(const std::shared_ptr<Engine::Objects::Actor>& actor, const std::string& name);

        void SetComponentActive(const std::shared_ptr<Component>& component, bool active);

        /// Runs an edit of the actor that has no command of its own (adding or removing a component...) : the
        /// actor is saved before and after it
        void EditActor(const std::shared_ptr<Engine::Objects::Actor>& actor, const std::string& label, const std::function<void()>& edit);

        void MarkWorldDirty();
    }

    /// @brief What Copy and Cut take : a description of the actor (not the actor itself, which can change or be
    /// destroyed before the paste), where it was in the world and under which parent.
    class ActorClipboard {
        public:
            void Copy(const std::shared_ptr<Engine::Objects::Actor>& actor);

            /// Copies the actor and deletes it
            void Cut(const std::shared_ptr<Engine::Objects::Actor>& actor);

            /// A new actor built from what was copied, at the same place in the world, under the same parent if it is
            /// still there (else at the root of the world). Null if there is nothing to paste.
            std::shared_ptr<Engine::Objects::Actor> Paste();

            bool IsEmpty() const { return data.is_null(); }

        private:
            ordered_json data;
            glm::mat4 worldMatrix = glm::mat4(1.0f);
            std::shared_ptr<ObjectRef> parent;
    };
}
