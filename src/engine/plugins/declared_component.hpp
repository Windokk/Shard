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

    class DeclaredComponentType {
    public:
        static constexpr size_t kMaxStorage = 1024;
        static constexpr size_t kMaxFields = 64;

        static Core::Result<std::shared_ptr<DeclaredComponentType>> Build(const DeclaredComponent& declaration);

        ~DeclaredComponentType();

        const std::string& Name() const { return m_Descriptor.name; }
        const ClassDescriptor& Descriptor() const { return m_Descriptor; }
        const std::vector<DeclaredField>& Fields() const { return m_Declared; }
        const FieldInfo* FindField(const std::string& name) const;

        static size_t StorageOffset();
        size_t StorageSize() const { return m_Size; }

        void ConstructDefaults(unsigned char* storage) const;
        void Destroy(unsigned char* storage) const;
        void Assign(unsigned char* to, const unsigned char* from) const;

    private:
        DeclaredComponentType() = default;
        ClassDescriptor m_Descriptor;
        std::deque<std::string> m_Names;
        std::deque<FieldInfo> m_FieldInfos;
        std::vector<DeclaredField> m_Declared;
        std::vector<unsigned char> m_Defaults;
        size_t m_Size = 0;
        size_t m_StorageBase = 0;
    };

    class DeclaredFieldsComponent : public Objects::Components::Component {
    public:
        DeclaredFieldsComponent(std::shared_ptr<Objects::Actor> parent, uint32_t localId, std::shared_ptr<const DeclaredComponentType> type);
        ~DeclaredFieldsComponent() override;

        std::shared_ptr<Objects::Components::Component> Clone() const override;
        const ClassDescriptor* GetDescriptor() const override { return &m_Type->Descriptor(); }

        const DeclaredComponentType& Type() const { return *m_Type; }

        nlohmann::json Get(const std::string& field) const;
        bool Set(const std::string& field, const nlohmann::json& value);
        nlohmann::json Values() const;

    private:
        alignas(16) unsigned char m_Storage[DeclaredComponentType::kMaxStorage];
        std::shared_ptr<const DeclaredComponentType> m_Type;
        friend class DeclaredComponentType;
    };

    Objects::Components::ComponentFactory MakeDeclaredFactory(std::shared_ptr<const DeclaredComponentType> type);
}
