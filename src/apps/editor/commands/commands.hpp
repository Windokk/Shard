#pragma once

#include <iostream>
#include <stack>
#include <memory>
#include <cstring>
#include <cassert>

#include "engine/assets/reflection/reflection_fields.hpp"

#include "engine/world/components/transform.hpp"
#include "apps/editor/commands/object_ref.hpp"

#include <algorithm>
#include <functional>
#include <vector>
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

    /// Bytes a value of this field takes, 0 for what a ValueBuffer can't hold (containers, structs, C strings)
    inline size_t GetFieldValueSize(const FieldInfo* f) {
        if (!f || !f->CopyConstruct || !f->Assign || !f->Destroy || !f->Equals)
            return 0;
        if (f->type == TypeID::Enum)
            return f->enumDesc ? f->enumDesc->size : 0;
        if (f->type == TypeID::CString)
            return 0;
        return GetTypeSize(f->type);
    }

    /// Whether the undo history can record the edits of this field (see ModifyFieldCommand)
    inline bool CanRecordField(const FieldInfo* f) {
        return GetFieldValueSize(f) > 0;
    }

    struct ValueBuffer {
        const FieldInfo* field;
        std::unique_ptr<uint8_t[]> storage;
        bool constructed = false;

        explicit ValueBuffer(const FieldInfo* f)
            : field(f),
            storage(std::make_unique<uint8_t[]>(std::max<size_t>(GetFieldValueSize(f), 1))) {}

        ValueBuffer(ValueBuffer&& other) noexcept
            : field(other.field), storage(std::move(other.storage)), constructed(other.constructed) {
            other.constructed = false;
        }

        ValueBuffer(const ValueBuffer&) = delete;
        ValueBuffer& operator=(const ValueBuffer&) = delete;
        ValueBuffer& operator=(ValueBuffer&&) = delete;

        void Capture(const void* source) {
            Destroy();
            if (!CanRecordField(field))
                return;
            field->CopyConstruct(storage.get(), source);
            constructed = true;
        }

        void AssignTo(void* destination) const {
            assert(constructed);
            field->Assign(destination, storage.get());
        }

        bool Equals(const ValueBuffer& other) const {
            return constructed && other.constructed && field->Equals(storage.get(), other.storage.get());
        }

        /// Whether the value held is the one at `value`
        bool EqualsValue(const void* value) const {
            return constructed && field->Equals(storage.get(), value);
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

    /// Where a field is in its component : in the component itself (null), or somewhere inside it (the fields of an InstancedStruct)
    using FieldOwnerLocator = std::function<void*(Component& component)>;

    /// One reflected field of a component, from the value it had to the one it has at Finalize
    class ModifyFieldCommand final : public Command {
        public:
            ModifyFieldCommand(const std::shared_ptr<Component>& comp, const FieldInfo* f, FieldOwnerLocator locator = nullptr)
                : component(ObjectRefs::Get(comp)),
                locate(std::move(locator)),
                field(f),
                before(f),
                after(f),
                name(f->name)
            {
                if (void* value = GetFieldPointer(comp.get()))
                    before.Capture(value);
            }

            /// For an edit noticed once the widget had already written the field : the caller kept the old value
            ModifyFieldCommand(const std::shared_ptr<Component>& comp, const FieldInfo* f, ValueBuffer&& oldValue, FieldOwnerLocator locator = nullptr)
                : component(ObjectRefs::Get(comp)),
                locate(std::move(locator)),
                field(f),
                before(std::move(oldValue)),
                after(f),
                name(f->name)
            {}

            void Finalize() override {
                if (finalized) return;

                if (auto comp = component ? component->Resolve<Component>() : nullptr)
                    if (void* value = GetFieldPointer(comp.get()))
                        after.Capture(value);

                finalized = true;
            }

            bool IsValid() const override {
                return before.constructed && after.constructed && !before.Equals(after);
            }

            void Undo() override {
                Apply(before);
            }

            void Redo() override {
                Apply(after);
            }

            const std::string& GetName() const override {
                return name;
            }

        private:
            void Apply(const ValueBuffer& value) {
                auto comp = component ? component->Resolve<Component>() : nullptr;
                if (!comp)
                    return;

                if (void* destination = GetFieldPointer(comp.get())) {
                    value.AssignTo(destination);
                    comp->OnFieldChanged({field});
                }
            }

            void* GetFieldPointer(Component* comp) const {
                if (!comp)
                    return nullptr;
                void* owner = locate ? locate(*comp) : static_cast<void*>(comp);
                return owner ? FieldRead(*field, owner) : nullptr;
            }

        private:
            std::shared_ptr<ObjectRef> component;
            FieldOwnerLocator locate;
            const FieldInfo* field;

            ValueBuffer before;
            ValueBuffer after;

            bool finalized = false;
            std::string name;
        };

}
