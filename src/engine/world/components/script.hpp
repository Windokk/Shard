#pragma once

#include "engine/world/components/component.hpp"

#include "engine/world/event_system.hpp"

#include "engine/assets/reflection/attributes.hpp"

namespace Shard::Engine::Events
{
    struct ContactAddedEvent;
    struct ContactPersistedEvent;
    struct ContactRemovedEvent;
}

namespace Shard::Engine::Objects::Components
{
    class Script : public Component{
        public:
            Script(std::shared_ptr<Actor> parent, uint32_t local_id);

            void Destroy() override;

            // Only called on actors with tag "Persistent"
            virtual void OnWorldLoaded();

            virtual void OnWorldUnloaded();

            virtual void OnCreate();

            virtual void OnPlay();

            virtual void OnTick();

            virtual bool RunsInEditMode() const;

            virtual void OnEditTick();

            // Runs 0..N times per frame depending on the accumulator. Use GetFixedDeltaTime(). Forces and reproducible gameplay go here.
            virtual void OnFixedTick();

            virtual void OnLateTick();

            virtual void OnStop();

            virtual void OnDestroyed();

            // Needs a PhysicsBody on the actor or a child. A child's callbacks run before the parent's.
            virtual void OnContactAdded(const Events::ContactAddedEvent& event);
            
            virtual void OnContactPersisted(const Events::ContactPersistedEvent& event);

            virtual void OnContactEnded(const Events::ContactRemovedEvent& event);

            virtual void OnActivated();

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