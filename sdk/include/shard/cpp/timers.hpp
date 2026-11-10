// Shard C++ SDK : delays, repeating timers and coroutines (shard_timers.h).
//
//   Shard::Timers::After(2.0, [] { Explode(); });                                  // once, after 2 s of simulation
//   auto spawn = Shard::Timers::Every(0.5, [] { Spawn(); });                       // every 0.5 s ; Shard::Timers::Cancel(spawn) stops it
//
//   Shard::Timers::Start(Shard::Sequence().Do(Open).Delay(1.0).Do(Close).Build()); // a sequence of actions and waits
//
//   Shard::Timers::Start([state = 0]() mutable -> Shard::Wait {                    // C++17 has no co_await : a coroutine is a step function
//       switch (state++) {
//           case 0: Warn();  return Shard::Wait::Seconds(1.0);
//           case 1: Fire();  return Shard::Wait::Until([] { return Reloaded(); }, 5.0);    // a condition, polled each step, with a time-out
//           default:         return Shard::Wait::Done();
//       }
//   });
//
//   Shard::EventLatch hit("player.hit");                                           // remembers the event from now on
//   Shard::Timers::Start(Shard::Sequence().UntilEvent(hit, 3.0).Do(Retaliate).Build());
//
// Time is the simulation's, in the fixed step : the time scale acts on it and a replay reproduces it. The callbacks run like a script's. A script
// has the same functions (After, Every, StartCoroutine) with the timers tied to its life. thread : main or script.
#pragma once

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <shard/shard_timers.h>
#include <shard/cpp/detail.hpp>
#include <shard/cpp/time.hpp>

namespace Shard {

    /// Tells the host, once the event named `name` was emitted (shard_events.h) at least once since this was made, so an event that comes before the
    /// coroutine reaches its wait is not lost. Copies share the latch, released with the last one.
    class EventLatch {
    public:
        /// A latch that never fires (what a Wait that is not an Event holds)
        EventLatch() = default;
        explicit EventLatch(const std::string& name)
        {
            ShardLatch latch = 0;
            detail::Check(Api().event_latch(detail::PluginHandle(), Str(name), &latch), "latch on event '" + name + "'");
            m_State = std::make_shared<State>(latch);
        }

        bool Fired() const { return m_State && Api().latch_fired(detail::PluginHandle(), m_State->latch) == SHARD_TRUE; }
        ShardLatch Handle() const { return m_State ? m_State->latch : 0; }

    private:
        struct State {
            explicit State(ShardLatch l) : latch(l) {}
            ~State() { Api().latch_release(detail::PluginHandle(), latch); }
            State(const State&) = delete;
            State& operator=(const State&) = delete;
            ShardLatch latch;
        };
        static const ShardTimersApi& Api() { return detail::Require<ShardTimersApi>(SHARD_IFACE_TIMERS); }
        std::shared_ptr<State> m_State;
    };

    /// What a coroutine step asks for next
    struct Wait {
        enum class Kind { Done, Seconds, Until, Event };
        Kind kind = Kind::Done;
        double seconds = 0.0;                   ///< the delay, or the time-out of an Until / Event (negative or infinite : none)
        std::function<bool()> until;
        EventLatch latch;

        static Wait Done() { return Wait(); }
        static Wait Seconds(double s) { Wait w; w.kind = Kind::Seconds; w.seconds = s; return w; }
        static Wait NextStep() { return Seconds(0.0); }
        /// Resumes on the first step where pred() is true, or after `timeout` seconds. Polled each fixed step, first on the step after the return.
        static Wait Until(std::function<bool()> pred, double timeout = std::numeric_limits<double>::infinity())
        {
            Wait w; w.kind = Kind::Until; w.seconds = timeout; w.until = std::move(pred); return w;
        }
        /// Resumes once the latch fired, or after `timeout` seconds. latch.Fired() tells which on resume.
        static Wait Event(const EventLatch& latch, double timeout = std::numeric_limits<double>::infinity())
        {
            Wait w; w.kind = Kind::Event; w.seconds = timeout; w.latch = latch; return w;
        }
    };

    using Coroutine = std::function<Wait()>;

    /// Builds a coroutine from a list of actions and waits : Sequence().Do(a).Delay(2).Do(b).Build()
    class Sequence {
    public:
        Sequence& Do(std::function<void()> fn) { m_Items.push_back({ std::move(fn), Wait::Done() }); return *this; }
        Sequence& Delay(double seconds) { m_Items.push_back({ nullptr, Wait::Seconds(seconds) }); return *this; }
        Sequence& Until(std::function<bool()> pred, double timeout = std::numeric_limits<double>::infinity())
        {
            m_Items.push_back({ nullptr, Wait::Until(std::move(pred), timeout) });
            return *this;
        }
        Sequence& UntilEvent(const EventLatch& latch, double timeout = std::numeric_limits<double>::infinity())
        {
            m_Items.push_back({ nullptr, Wait::Event(latch, timeout) });
            return *this;
        }
        Coroutine Build() const
        {
            auto items = std::make_shared<const std::vector<Item>>(m_Items);
            auto index = std::make_shared<size_t>(0);
            return [items, index]() -> Wait {
                while (*index < items->size()) {
                    const Item& item = (*items)[(*index)++];
                    if (item.fn) item.fn();
                    else return item.wait;
                }
                return Wait::Done();
            };
        }

    private:
        struct Item {
            std::function<void()> fn;
            Wait wait;
        };
        std::vector<Item> m_Items;
    };

    class Timers {
    public:
        /// Runs fn once after `seconds`. Throws Shard::Error if no world is loaded or the delay is not finite.
        static ShardTimer After(double seconds, std::function<void()> fn)
        {
            Sweep();
            auto* c = Make();
            c->once = std::move(fn);
            ShardTimer timer = 0;
            const ShardResult r = Api().after(detail::PluginHandle(), seconds, &Timers::OnceThunk, c, &timer);
            return Register(c, timer, r, "a delay");
        }

        /// Runs fn every `interval` seconds, the first time after one interval
        static ShardTimer Every(double interval, std::function<void()> fn)
        {
            Sweep();
            auto* c = Make();
            c->once = std::move(fn);
            ShardTimer timer = 0;
            const ShardResult r = Api().every(detail::PluginHandle(), interval, &Timers::EveryThunk, c, &timer);
            return Register(c, timer, r, "a repeating timer");
        }

        /// Starts a coroutine : its first step runs on the next fixed step, and it runs again when its Wait is over, until it returns Wait::Done()
        static ShardTimer Start(Coroutine step)
        {
            Sweep();
            auto* c = Make();
            c->step = std::move(step);
            ShardTimer timer = 0;
            const ShardResult r = Api().start(detail::PluginHandle(), &Timers::StepThunk, c, &timer);
            return Register(c, timer, r, "a coroutine");
        }

        /// Safe from inside a callback, the running one included. False if it finished or was cancelled already.
        static bool Cancel(ShardTimer timer)
        {
            const bool ok = Api().cancel(detail::PluginHandle(), timer) == SHARD_OK;
            Forget(timer);
            return ok;
        }
        static bool IsActive(ShardTimer timer) { return Api().is_active(detail::PluginHandle(), timer) == SHARD_TRUE; }
        /// Every timer of this plugin
        static void CancelAll()
        {
            Api().cancel_all(detail::PluginHandle());
            for (auto it = Closures().begin(); it != Closures().end();) {
                if (it->second->running) { ++it; continue; }
                it = Closures().erase(it);
            }
        }

    private:
        // What the host calls back. One per timer, freed when it finishes, is cancelled, or is found dead by a sweep (the host drops a world's timers silently).
        struct Closure {
            std::function<void()> once;
            Coroutine step;
            ShardTimer timer = 0;
            bool running = false;
            bool polling = false;
            EventLatch latch;                           // kept while the host waits on it, whatever the coroutine does with its own copy
            double remaining = 0.0;                     // seconds left before an Until times out, counted a fixed step at a time
            std::function<bool()> pred;
        };
        using Table = std::unordered_map<ShardTimer, std::unique_ptr<Closure>>;

        static Table& Closures()
        {
            static Table table;
            return table;
        }
        static std::vector<std::unique_ptr<Closure>>& Pending()
        {
            static std::vector<std::unique_ptr<Closure>> pending;
            return pending;
        }
        static Closure* Make()
        {
            Pending().push_back(std::make_unique<Closure>());
            return Pending().back().get();
        }
        static ShardTimer Register(Closure* c, ShardTimer timer, ShardResult r, const char* what)
        {
            auto& pending = Pending();
            std::unique_ptr<Closure> owned;
            for (auto it = pending.begin(); it != pending.end(); ++it)
                if (it->get() == c) { owned = std::move(*it); pending.erase(it); break; }
            detail::Check(r, std::string(what) + " could not be started");
            c->timer = timer;
            Closures()[timer] = std::move(owned);
            return timer;
        }
        static void Forget(ShardTimer timer)
        {
            auto it = Closures().find(timer);
            if (it != Closures().end() && !it->second->running) Closures().erase(it);
        }
        // Closures of timers the host dropped (their world went) are found when the table has doubled since the last time
        static void Sweep()
        {
            static size_t threshold = 64;
            Table& table = Closures();
            if (table.size() < threshold) return;
            for (auto it = table.begin(); it != table.end();) {
                if (!it->second->running && !IsActive(it->first)) it = table.erase(it);
                else ++it;
            }
            threshold = std::max<size_t>(64, table.size() * 2);
        }

        struct Running {
            explicit Running(Closure* c) : closure(c) { closure->running = true; }
            ~Running() { closure->running = false; }
            Closure* closure;
        };

        static void SHARD_CALL OnceThunk(void* user)
        {
            auto* c = static_cast<Closure*>(user);
            std::function<void()> fn = std::move(c->once);
            Forget(c->timer);                                   // c is gone : only fn is used from here
            detail::NoThrow([&] { fn(); });
        }

        static void SHARD_CALL EveryThunk(void* user)
        {
            auto* c = static_cast<Closure*>(user);
            Running guard(c);
            detail::NoThrow([&] { c->once(); });
        }

        static void SHARD_CALL StepThunk(void* user, ShardWait* wait)
        {
            auto* c = static_cast<Closure*>(user);
            const ShardTimer timer = c->timer;
            bool finished = false;
            {
                Running guard(c);
                finished = Step(*c, *wait);
            }
            if (finished) Forget(timer);
        }

        // True when the coroutine is over
        static bool Step(Closure& c, ShardWait& wait)
        {
            if (c.polling) {
                // Counted by the polls themselves, one per fixed step : shard.time's simTime follows the physics steps, which a world may not run
                const double step = Time::Now().fixedDeltaTime;
                c.remaining -= step;
                bool ready = c.remaining <= step * 1e-3;
                if (!ready) detail::NoThrow([&] { ready = c.pred(); });
                if (!ready) { wait.kind = SHARD_WAIT_SECONDS; wait.seconds = 0.0; return false; }
                c.polling = false;
                c.pred = nullptr;
            }
            Wait next;
            bool threw = true;
            detail::NoThrow([&] { next = c.step(); threw = false; });
            if (threw) return true;
            switch (next.kind) {
                case Wait::Kind::Seconds:
                    wait.kind = SHARD_WAIT_SECONDS;
                    wait.seconds = next.seconds;
                    return false;
                case Wait::Kind::Until:
                    c.polling = true;
                    c.pred = std::move(next.until);
                    c.remaining = next.seconds >= 0.0 ? next.seconds : std::numeric_limits<double>::infinity();
                    wait.kind = SHARD_WAIT_SECONDS;
                    wait.seconds = 0.0;
                    return false;
                case Wait::Kind::Event:
                    wait.kind = SHARD_WAIT_LATCH;
                    c.latch = next.latch;
                    wait.latch = c.latch.Handle();
                    wait.seconds = next.seconds >= 0.0 && next.seconds < std::numeric_limits<double>::infinity() ? next.seconds : -1.0;
                    return false;
                default:
                    return true;
            }
        }

        static const ShardTimersApi& Api() { return detail::Require<ShardTimersApi>(SHARD_IFACE_TIMERS); }
    };

    /// The timers a script owns, cancelled with it. A copy starts empty (a cloned script has not made the timers of the original).
    class TimerSet {
    public:
        TimerSet() = default;
        TimerSet(const TimerSet&) {}
        TimerSet& operator=(const TimerSet&) { return *this; }
        ~TimerSet() { CancelAll(); }

        void Add(ShardTimer timer)
        {
            if (m_Timers.size() >= m_SweepAt) {
                m_Timers.erase(std::remove_if(m_Timers.begin(), m_Timers.end(), [](ShardTimer t) { return !Timers::IsActive(t); }), m_Timers.end());
                m_SweepAt = std::max<size_t>(32, m_Timers.size() * 2);
            }
            m_Timers.push_back(timer);
        }
        bool Cancel(ShardTimer timer)
        {
            m_Timers.erase(std::remove(m_Timers.begin(), m_Timers.end(), timer), m_Timers.end());
            return Timers::Cancel(timer);
        }
        void CancelAll()
        {
            for (ShardTimer t : m_Timers) Timers::Cancel(t);
            m_Timers.clear();
        }

    private:
        std::vector<ShardTimer> m_Timers;
        size_t m_SweepAt = 32;
    };
}
