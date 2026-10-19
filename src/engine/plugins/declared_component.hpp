#pragma once

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/core/containers/result.hpp"
#include "engine/plugins/content.hpp"
#include "engine/world/components/component.hpp"
#include "engine/world/components/registry/component_registry.hpp"

namespace Shard::Engine::Plugins {

    /// @brief What a datapack's component declaration turns into : the reflection descriptor of a component made of fields, and where each field
    /// lives in the component's storage. Built once per declared type, shared by every instance.
    class DeclaredComponentType {
    public:
        static constexpr size_t kMaxStorage = 1024;
        static constexpr size_t kMaxFields = 64;

        /// @return the type, or why the declaration cannot be built (a reserved or repeated field name, too many fields, too much storage)
        static Core::Result<std::shared_ptr<DeclaredComponentType>> Build(const DeclaredComponent& declaration);

        ~DeclaredComponentType();

        const std::string& Name() const { return m_Descriptor.name; }
        const ClassDescriptor& Descriptor() const { return m_Descriptor; }
        const std::vector<DeclaredField>& Fields() const { return m_Declared; }
        const FieldInfo* FindField(const std::string& name) const;

        /// Offset of the field storage inside DeclaredFieldsComponent
        static size_t StorageOffset();
        size_t StorageSize() const { return m_Size; }

        /// Constructs every field at `storage` from its declared default
        void ConstructDefaults(unsigned char* storage) const;
        void Destroy(unsigned char* storage) const;
        void Assign(unsigned char* to, const unsigned char* from) const;

    private:
        DeclaredComponentType() = default;
        ClassDescriptor m_Descriptor;
        std::deque<std::string> m_Names;                 // FieldInfo::name points into these
        std::deque<FieldInfo> m_FieldInfos;
        std::vector<DeclaredField> m_Declared;
        std::vector<unsigned char> m_Defaults;           // a constructed set of default values, copied into every instance
        size_t m_Size = 0;
        size_t m_StorageBase = 0;
    };

    /// @brief A component whose fields a datapack declared : saved, loaded, copied and shown in the inspector through the same reflection as
    /// the components written in C++, readable from code by name.
    class DeclaredFieldsComponent : public Objects::Components::Component {
    public:
        DeclaredFieldsComponent(std::shared_ptr<Objects::Actor> parent, uint32_t localId, std::shared_ptr<const DeclaredComponentType> type);
        ~DeclaredFieldsComponent() override;

        std::shared_ptr<Objects::Components::Component> Clone() const override;
        const ClassDescriptor* GetDescriptor() const override { return &m_Type->Descriptor(); }

        const DeclaredComponentType& Type() const { return *m_Type; }

        /// The value of a field as JSON (null for a name the type doesn't have)
        nlohmann::json Get(const std::string& field) const;
        /// Sets a field from JSON of the right shape. False (nothing changed) if there is no such field or the value doesn't fit.
        bool Set(const std::string& field, const nlohmann::json& value);
        /// Every field, by name
        nlohmann::json Values() const;

    private:
        alignas(16) unsigned char m_Storage[DeclaredComponentType::kMaxStorage];
        std::shared_ptr<const DeclaredComponentType> m_Type;
        friend class DeclaredComponentType;
    };

    /// The factory to register a declared type under its name
    Objects::Components::ComponentFactory MakeDeclaredFactory(std::shared_ptr<const DeclaredComponentType> type);
}
