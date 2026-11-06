#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "engine/world/object.hpp"
#include "engine/core/tags/tag_container.hpp"

using namespace nlohmann;
using ordered_json = nlohmann::ordered_json;

struct ClassDescriptor;

namespace Shard::Engine::Core{
    class IEngineContext;
}

namespace Shard::Engine::Objects{
    class Actor;

    namespace Components{

        template<typename BaseType, typename DerivedType>
        constexpr bool IsSubclassOf() {
            return std::is_base_of<BaseType, DerivedType>::value;
        }

        // Bit flags. Hierarchy: an ancestor changed, so this world matrix did.
        enum TransformChange : uint8_t {
            TransformPosition  = 1 << 0,
            TransformRotation  = 1 << 1,
            TransformScale     = 1 << 2,
            TransformHierarchy = 1 << 3
        };

        class Component : public Core::Object, public Core::TagContainer::Listener{

            public:
                Component(std::shared_ptr<Actor> parent, uint32_t local_id);
                Component(const Component& other);
                Component& operator=(const Component& other);
                virtual ~Component();
                std::shared_ptr<Actor> parent = nullptr;

                Core::IEngineContext* GetEngineContext() const;

                virtual void Activate() { activated = true; }

                virtual void DeActivate() { activated = false; }

                virtual void Deserialize(const json componentData);

                virtual ordered_json Serialize();

                template<typename T>
                bool IsInstanceOf() const {
                    return dynamic_cast<const T*>(this) != nullptr;
                }

                bool Active(){
                    return activated;
                }

                void SetLocalId(uint32_t newLocalId){
                    local_id = newLocalId;
                }

                uint32_t GetLocalId(){
                    return local_id;
                }

                virtual std::shared_ptr<Component> Clone() const = 0;

                void SetParent(std::shared_ptr<Actor> newParent){
                    parent = newParent;
                }

                virtual const ClassDescriptor* GetDescriptor() const = 0;

                virtual void OnAttached() {}

                virtual void OnTransformChanged(uint8_t changes) {}

                Core::TagContainer& GetTags() { return tags; }
                const Core::TagContainer& GetTags() const { return tags; }

                void OnTagsChanged(const Core::TagContainer& added, const Core::TagContainer& removed, bool quiet) override;

                bool ApplyField(const std::string& name, const json& value);

            private:

            protected:
                // "type", "active" and "tags" are reserved keys, a reflected field with such a name is not serialized.
                ordered_json SerializeReflected(const std::string& typeName) const;

                void DeserializeReflected(const json& componentData, bool defaultActive = true);

                void DeserializeReflectedFields(const json& componentData, bool notify = true);
                void DeserializeActive(const json& componentData, bool defaultActive);

                uint32_t local_id;
                bool activated = true;
                Core::TagContainer tags;

        };
    }
    
}