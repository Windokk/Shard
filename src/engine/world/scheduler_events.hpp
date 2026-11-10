#pragma once

#include <functional>
#include <memory>

#include "engine/world/event_system.hpp"
#include "engine/world/scheduler.hpp"

namespace Shard::Engine::Worlds{

    // Latches the first T emitted (matching the optional filter) from the moment of construction, to wait on it in a coroutine :
    //   auto hit = EventWait<Events::ContactAddedEvent>::ForActor(dispatcher, actorId);
    //   StartCoroutine(Sequence().Until(hit.Predicate(), 5.0).Do([]{ ... }).Build());
    // Copies share one subscription, dropped when the last copy dies or the event arrives. The dispatcher must outlive them (worlds unload before it).
    template<class T>
    class EventWait{
        public:
            using Filter = std::function<bool(const T&)>;

            static EventWait Global(Events::EventDispatcher& dispatcher, Filter filter = nullptr)
            {
                return EventWait(dispatcher, std::move(filter), [](Events::EventDispatcher& d, std::function<void(const T&)> cb){
                    return d.subscribeGlobal<T>(std::move(cb));
                });
            }

            static EventWait ForActor(Events::EventDispatcher& dispatcher, Core::ObjectID actor, Filter filter = nullptr)
            {
                return EventWait(dispatcher, std::move(filter), [actor](Events::EventDispatcher& d, std::function<void(const T&)> cb){
                    return d.subscribeToActor<T>(actor, std::move(cb));
                });
            }

            bool Fired() const { return state->fired; }

            std::function<bool()> Predicate() const
            {
                return [s = state]{ return s->fired; };
            }

            // Returns Wait::Until(Fired). Check Fired() on resume to tell the event from the timeout.
            Wait Until(double timeout = std::numeric_limits<double>::infinity()) const
            {
                return Wait::Until(Predicate(), timeout);
            }

        private:
            struct State{
                Events::EventDispatcher* dispatcher = nullptr;
                Events::EventDispatcher::Subscription token = 0;
                bool fired = false;

                ~State()
                {
                    if(dispatcher && token)
                        dispatcher->Unsubscribe(token);
                }
            };

            template<class Subscribe>
            EventWait(Events::EventDispatcher& dispatcher, Filter filter, Subscribe subscribe) : state(std::make_shared<State>())
            {
                state->dispatcher = &dispatcher;
                // Weak, or the dispatcher would keep the state alive and the subscription could never be dropped.
                std::weak_ptr<State> weak = state;
                state->token = subscribe(dispatcher, [weak, filter = std::move(filter)](const T& event){
                    auto s = weak.lock();
                    if(!s || s->fired || (filter && !filter(event)))
                        return;
                    s->fired = true;
                    // Unsubscribed from inside the handler, which the dispatcher allows.
                    s->dispatcher->Unsubscribe(s->token);
                    s->token = 0;
                });
            }

            std::shared_ptr<State> state;
    };
}
