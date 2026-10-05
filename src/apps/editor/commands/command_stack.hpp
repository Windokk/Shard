#pragma once

#include "commands.hpp"

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

            void Undo() {
                if (undoStack.empty()) return;

                auto cmd = std::move(undoStack.back());
                undoStack.pop_back();

                cmd->Undo();
                redoStack.push_back(std::move(cmd));
            }

            void Redo() {
                if (redoStack.empty()) return;

                auto cmd = std::move(redoStack.back());
                redoStack.pop_back();

                cmd->Redo();
                undoStack.push_back(std::move(cmd));
            }

            bool IsActive() const {
                return !activeStack.empty();
            }

        private:
            void PushToHistory(std::unique_ptr<Command> cmd) {

                undoStack.push_back(std::move(cmd));
                redoStack.clear();

                if (undoStack.size() > MaxHistory)
                    undoStack.erase(undoStack.begin());
            }

            static constexpr size_t MaxHistory = 256;

            std::vector<std::unique_ptr<CompositeCommand>> activeStack;
            std::vector<std::unique_ptr<Command>> undoStack;
            std::vector<std::unique_ptr<Command>> redoStack;
    };
}