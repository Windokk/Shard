#pragma once

#include <iostream>
#include <stack>
#include <memory>
#include <cstring>
#include <cassert>

#include "engine/assets/reflection/reflection_fields.hpp"

#include "engine/world/components/transform.hpp"
#include <glm/gtx/string_cast.hpp>

using namespace Shard::Engine::Objects::Components;

namespace Shard::Editor::Commands{

    class Command {
        public:
            virtual ~Command() = default;

            // Called after creation but before push
            virtual void Finalize() {}

            virtual void Undo() = 0;
            virtual void Redo() = 0;

            virtual bool IsValid() const { return true; }

            virtual const std::string& GetName() const = 0;
    };

    class CompositeCommand final : public Command {
        public:
            explicit CompositeCommand(std::string label)
                : name(std::move(label)) {}

            void Add(std::unique_ptr<Command> cmd) {
                commands.push_back(std::move(cmd));
            }

            void Finalize() override {
                for (auto& cmd : commands)
                    cmd->Finalize();

                commands.erase(
                    std::remove_if(commands.begin(), commands.end(),
                        [](auto& c) { return !c->IsValid(); }),
                    commands.end());
            }

            bool IsValid() const override {
                return !commands.empty();
            }

            void Undo() override {
                for (auto it = commands.rbegin(); it != commands.rend(); ++it)
                    (*it)->Undo();
            }

            void Redo() override {
                for (auto& cmd : commands)
                    cmd->Redo();
            }

            const std::string& GetName() const override {
                return name;
            }

        private:
            std::string name;
            std::vector<std::unique_ptr<Command>> commands;
    };

    struct ValueBuffer {
        const FieldInfo* field;
        std::unique_ptr<uint8_t[]> storage;
        bool constructed = false;

        explicit ValueBuffer(const FieldInfo* f)
            : field(f),
            storage(std::make_unique<uint8_t[]>(GetTypeSize(f->type))) {}

        void Capture(const void* source) {
            Destroy();
            field->CopyConstruct(storage.get(), source);
            constructed = true;
        }

        void AssignTo(void* destination) const {
            assert(constructed);
            field->Assign(destination, storage.get());
        }

        bool Equals(const ValueBuffer& other) const {
            return field->Equals(storage.get(), other.storage.get());
        }

        void Destroy() {
            if (constructed) {
                field->Destroy(storage.get());
                constructed = false;
            }
        }

        ~ValueBuffer() {
            Destroy();
        }
    };

    using ObjectResolver = std::function<void*(uint32_t id)>;

    class ModifyFieldCommand final : public Command {
        public:
            ModifyFieldCommand(
                uint32_t id,
                ObjectResolver resolverFunc,
                const FieldInfo* f, std::shared_ptr<Component> comp)
                : objectID(id),
                resolver(std::move(resolverFunc)),
                field(f),
                before(f),
                after(f),
                name(f->name),
                component(comp)
            {
                if (void* obj = Resolve())
                    before.Capture(GetFieldPointer(obj));
            }

            void Finalize() override {
                if (finalized) return;

                if (void* obj = Resolve()){
                    after.Capture(GetFieldPointer(obj));
                }
                finalized = true;
            }

            bool IsValid() const override {
                bool valid = !before.Equals(after);
                return valid;
            }

            void Undo() override {
                if (void* obj = Resolve()){
                    before.AssignTo(GetFieldPointer(obj));
                    component->OnFieldChanged({field});
                }
            }

            void Redo() override {
                if (void* obj = Resolve()){
                    after.AssignTo(GetFieldPointer(obj));
                    component->OnFieldChanged({field});
                }
            }

            const std::string& GetName() const override {
                return name;
            }

        private:
            void* Resolve() const {
                return resolver ? resolver(objectID) : nullptr;
            }

            void* GetFieldPointer(void* obj) const {
                return FieldRead(*field, obj);
            }

        private:
            uint32_t objectID;
            ObjectResolver resolver;
            const FieldInfo* field;
            std::shared_ptr<Component> component;

            ValueBuffer before;
            ValueBuffer after;

            bool finalized = false;
            std::string name;
        };

}
