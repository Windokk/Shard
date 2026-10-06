#pragma once

#include "engine/world/objectID.hpp"

struct FieldChangedEvent;

namespace Shard::Engine::Core{

    class IEngineContext;

    /// @brief The base class for every type of object (asset instance, world, world object...)
    class Object : public std::enable_shared_from_this<Object> {
        public:
            virtual ~Object() = default;

            ObjectID GetID() const { return id; }

            static void AssignObjectID(std::shared_ptr<Object> obj);
            static void AssignObjectID(std::shared_ptr<Object> obj, IEngineContext* engine);

            template <typename T, typename... Args>
            static std::shared_ptr<T> Create(Args&&... args){
                static_assert(std::is_base_of<Object, T>::value, "T must derive from Object");
                std::shared_ptr<T> obj = std::make_shared<T>(std::forward<Args>(args)...);
                AssignObjectID(obj);
                Object::CallInit(obj);
                return obj;
            }

            template <typename T, typename... Args>
            static std::shared_ptr<T> CreateWithContext(IEngineContext* engine, Args&&... args){
                static_assert(std::is_base_of<Object, T>::value, "T must derive from Object");
                std::shared_ptr<T> obj = std::make_shared<T>(std::forward<Args>(args)...);
                AssignObjectID(obj, engine);
                Object::CallInit(obj);
                return obj;
            }

            // Reflection / editor
            virtual void OnFieldChanged(const FieldChangedEvent& event) {}

            template<typename T>
            std::shared_ptr<T> Cast()
            {
                static_assert(std::is_base_of<Object, T>::value);
                return std::dynamic_pointer_cast<T>(shared_from_this());
            }

            virtual void Destroy();

            template<typename T>
            bool Is() const
            {
                return dynamic_cast<const T*>(this) != nullptr;
            }

            template<typename T>
            std::shared_ptr<T> AsShared()
            {
                static_assert(std::is_base_of<Object, T>::value);
                return std::static_pointer_cast<T>(shared_from_this());
            }

        protected:
            Object() = default;
            
            ObjectID id;

        private:

            // Init() detection system

            template <typename, typename = std::void_t<>>
            struct HasInit : std::false_type {};

            template <typename T>
            struct HasInit<T, std::void_t<decltype(std::declval<T>().Init())>>
                : std::true_type {};

            // If T has Init()
            template <typename U>
            static std::enable_if_t<HasInit<U>::value>
            CallInit(std::shared_ptr<U> obj) {
                obj->Init();
            }

            // If T does NOT have Init()
            template <typename U>
            static std::enable_if_t<!HasInit<U>::value>
            CallInit(std::shared_ptr<U>) {
                // nothing
            }
    };
}