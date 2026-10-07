#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "engine/world/object.hpp"

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

        /// What moved when a Transform tells the other components of its actor that it changed (bit flags).
        /// Hierarchy : the transform did not change itself, an ancestor's did, so its world matrix did.
        enum TransformChange : uint8_t {
            TransformPosition  = 1 << 0,
            TransformRotation  = 1 << 1,
            TransformScale     = 1 << 2,
            TransformHierarchy = 1 << 3
        };

        class Component : public Core::Object{

            public:
                Component(std::shared_ptr<Actor> parent, uint32_t local_id);
                virtual ~Component();
                std::shared_ptr<Actor> parent = nullptr;

                Core::IEngineContext* GetEngineContext() const;

                /// @brief Activates this component
                virtual void Activate() { activated = true; }

                /// @brief Deactivates this component
                virtual void DeActivate() { activated = false; }

                /// @brief Loads this component from its serialized form. By default this reads the reflected fields
                /// (see DeserializeReflected), override it for a component whose state is not just its fields.
                virtual void Deserialize(const json componentData);

                /// @brief The serialized form of this component. By default the reflected fields, under the type name
                /// the component registered with (see SerializeReflected), override it for a component whose state is
                /// not just its fields.
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

                /// @brief Custom logic for duplication of the component (copy fields)
                /// @return A "copy" of this component
                /// @todo
                /// Should be replaced with automatic cloning using reflection
                /// @endinternal
                virtual std::shared_ptr<Component> Clone() const = 0;

                void SetParent(std::shared_ptr<Actor> newParent){
                    parent = newParent;
                }

                virtual const ClassDescriptor* GetDescriptor() const = 0;

                /// @brief The Transform of this component's actor changed
                /// @param changes TransformChange flags
                virtual void OnTransformChanged(uint8_t changes) {}

            private:

            protected:
                /// @brief Serializes the reflected fields of this component (its descriptor, see GetDescriptor)
                /// @param typeName The "type" the component is found again by when a level is loaded
                /// @note "type" and "active" are reserved keys : a reflected field named like that is not serialized
                ordered_json SerializeReflected(const std::string& typeName) const;

                /// @brief Loads the reflected fields of this component, running OnFieldChanged for each of them
                /// (the path an edit in the editor takes, so the component applies the value like it would for an edit).
                /// A field missing from the file keeps its default. The "active" state is applied last, once the
                /// fields are in place.
                /// @param defaultActive Whether the component is active when the file doesn't say
                void DeserializeReflected(const json& componentData, bool defaultActive = true);

                /// @brief The two halves of DeserializeReflected, for a component that has to do something between them
                /// @param notify Run OnFieldChanged for each field read (false : the component applies them itself)
                void DeserializeReflectedFields(const json& componentData, bool notify = true);
                void DeserializeActive(const json& componentData, bool defaultActive);

                uint32_t local_id;
                bool activated = true;

        };
    }
    
}