#pragma once

#include "engine/world/components/component.hpp"

#include "engine/events/event_system.hpp"

#include "engine/core/attributes.hpp"

namespace Shard::Engine::Objects::Components
{
    /// @brief Main scripting component. All scripts should inherit from this component. Scripts method are called from their parent actor, in order of addition
    class Script : public Component{
        public:
            Script(std::shared_ptr<Actor> parent, uint32_t local_id);

            void Destroy() override;

            /// @brief Called when the level is loaded
            /// @note Only called on actors with tag "Persistent" 
            virtual void OnLevelLoaded();

            /// @brief Called when the level is unloaded
            virtual void OnLevelUnloaded();

            /// @brief Called when this script component is added to an actor parent
            virtual void OnCreate();

            /// @brief Called when the play mode starts.
            virtual void OnPlay();

            /// @brief Called each frame
            virtual void OnTick();

            /// @brief Called when the play mode stops
            virtual void OnStop();

            /// @brief Called when the component is destroyed
            /// @note Also called when parent actor is destroyed
            virtual void OnDestroyed();

            /// @brief Callback for when the parent actor enters collision with something.
            /// @param event The descriptor for this collision event
            /// @details This can only be called if the parent actor has a PhysicsBody component, 
            /// or a child with PhysicsBody component. In the case of a child with PhysicsBody, the physics methods are called on the child first, then on the parent.
            virtual void OnContactAdded(const Events::ContactAddedEvent& event);
            
            /// @brief Callback for when the parent actor enters collision with something.
            /// @param event The descriptor for this collision event
            /// @details This can only be called if the parent actor has a PhysicsBody component, 
            /// or a child with PhysicsBody component. In the case of a child with PhysicsBody, the physics methods are called on the child first, then on the parent.
            virtual void OnContactPersisted(const Events::ContactPersistedEvent& event);

            /// @brief Callback for when the parent actor enters collision with something.
            /// @param event The descriptor for this collision event
            /// @details This can only be called if the parent actor has a PhysicsBody component, 
            /// or a child with PhysicsBody component. In the case of a child with PhysicsBody, the physics methods are called on the child first, then on the parent.
            virtual void OnContactEnded(const Events::ContactRemovedEvent& event);

            /// @brief Called when this component is activated / when this component's parent is activated
            virtual void OnActivated();

            /// @brief Called when this component is deactivated / when this component's parent is deactivated
            virtual void OnDeactivated();
            
            void Activate() override
            {
                Component::Activate();

                OnActivated();
            }

            void DeActivate() override
            {
                Component::DeActivate();

                OnDeactivated();
            }

            std::shared_ptr<Component> Clone() const override;

            DECLARE_DESCRIPTOR(Script)

        private:


    };
}