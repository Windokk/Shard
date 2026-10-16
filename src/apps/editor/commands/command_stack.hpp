#pragma once

#include "commands.hpp"

#include "engine/world/engine.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/world/world.hpp"

#include <algorithm>

namespace Shard::Editor::Commands{

    class CommandStack {
        public:
            static CommandStack& Get() {
                static CommandStack instance;
                return instance;
            }

            void Begin(const std::string& name) {
                activeStack.push_back(
                    std::make_unique<CompositeCommand>(name));
            }

            void Add(std::unique_ptr<Command> cmd) {
                assert(!activeStack.empty());
                activeStack.back()->Add(std::move(cmd));
            }

            void End() {
                assert(!activeStack.empty());

                auto finished = std::move(activeStack.back());
                activeStack.pop_back();

                finished->Finalize();

                if (!finished->IsValid())
                    return;

                if (!activeStack.empty()) {
                    activeStack.back()->Add(std::move(finished));
                    return;
                }

                PushToHistory(std::move(finished));
            }

            /// A command for an edit that is already done : into the group being recorded if there is one, else
            /// straight into the history
            void Record(std::unique_ptr<Command> cmd) {
                if (!cmd)
                    return;

                if (!activeStack.empty()) {
                    activeStack.back()->Add(std::move(cmd));
                    return;
                }

                cmd->Finalize();
                if (cmd->IsValid())
                    PushToHistory(std::move(cmd));
            }

            void Undo() {
                if (undoStack.empty() || !CanStep()) return;

                auto cmd = std::move(undoStack.back());
                undoStack.pop_back();

                cmd->Undo();
                redoStack.push_back(std::move(cmd));
                MarkWorldDirty();
            }

            void Redo() {
                if (redoStack.empty() || !CanStep()) return;

                auto cmd = std::move(redoStack.back());
                redoStack.pop_back();

                cmd->Redo();
                undoStack.push_back(std::move(cmd));
                MarkWorldDirty();
            }

            bool CanUndo() const { return !undoStack.empty() && CanStep(); }
            bool CanRedo() const { return !redoStack.empty() && CanStep(); }

            bool IsActive() const {
                return !activeStack.empty();
            }

            void Clear() {
                undoStack.clear();
                redoStack.clear();
                ObjectRefs::Clear();
            }

            /// How many actions Undo can go back (the Editor Preferences' undo history). Nothing is forgotten here : the oldest
            /// actions over the limit go when the next one is recorded, so a value dragged down then back up loses nothing.
            void SetMaxHistory(size_t count) {
                maxHistory = std::max<size_t>(count, 1);
            }

            size_t GetMaxHistory() const { return maxHistory; }

        private:
            /// Not in the middle of recording an edit (a gizmo drag), nor in play mode : the history is the edited world's
            bool CanStep() const {
                return activeStack.empty() && !Engine::Core::GetEngine().IsInPlayMode();
            }

            /// Undo and redo change the world like any other edit
            static void MarkWorldDirty() {
                if (auto* world = Engine::Core::GetEngine().GetWorldManager()->GetWorldAt(0))
                    world->SetDirty(true);
            }

            void PushToHistory(std::unique_ptr<Command> cmd) {

                undoStack.push_back(std::move(cmd));
                redoStack.clear();

                if (undoStack.size() > maxHistory)
                    undoStack.erase(undoStack.begin(), undoStack.begin() + (undoStack.size() - maxHistory));
            }

            size_t maxHistory = 256;

            std::vector<std::unique_ptr<CompositeCommand>> activeStack;
            std::vector<std::unique_ptr<Command>> undoStack;
            std::vector<std::unique_ptr<Command>> redoStack;
    };
}