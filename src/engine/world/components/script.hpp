#pragma once

#include "engine/world/components/component.hpp"

#include "engine/world/event_system.hpp"
#include "engine/world/scheduler.hpp"

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

            // Timers and coroutines of this script, run in the fixed step by the world's Scheduler. They die with the script, on Stop and on world unload.
            // Not paused while the script is inactive : check Active() in the callback, or call StopTimers() from OnDeactivated().
            // Return 0 when the script is not in a world, or from OnEditTick (the world does not simulate there).
            Worlds::TimerHandle After(double seconds, Worlds::Scheduler::Callback fn);
            Worlds::TimerHandle Every(double interval, Worlds::Scheduler::Callback fn);
            Worlds::TimerHandle StartCoroutine(Worlds::Scheduler::Coroutine step);
            bool StopTimer(Worlds::TimerHandle handle);
            void StopTimers();

            DECLARE_DESCRIPTOR(Script)

        private:

            Worlds::Scheduler* GetScheduler();

            // Kept apart from the world pointer, which outlives the world : the timers are stopped through it, and a world that is gone has none left to stop
            std::weak_ptr<Worlds::Scheduler> timers;


    };
}