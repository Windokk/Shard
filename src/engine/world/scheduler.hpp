#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace Shard::Engine::Worlds{

    // 0 is never a valid handle.
    using TimerHandle = uint64_t;

    struct Wait{
        double seconds = 0.0;
        bool done = false;
        std::function<bool()> until;

        static Wait Seconds(double s) { return { s, false, nullptr }; }
        static Wait NextStep() { return { 0.0, false, nullptr }; }
        static Wait Done() { return { 0.0, true, nullptr }; }

        // Resumes on the first step where pred() is true, or after `timeout` seconds. pred is polled each fixed step, first on the step after the return, so an already true pred still waits one step.
        static Wait Until(std::function<bool()> pred, double timeout = std::numeric_limits<double>::infinity()) { return { timeout, false, std::move(pred) }; }
    };

    // Delays, repeating timers and coroutines for one world. It advances in the fixed step, in simulated time: time scale 0 pauses it, replays and headless runs reproduce it.
    // Everything is cancelled when the world stops or unloads. Pass an owner to also cancel it when the owner dies.
    class Scheduler{
        public:
            using Callback = std::function<void()>;
            // C++17 has no co_await, so a coroutine is a step function. It runs on the step after Start, then each time its Wait elapses, until it returns Wait::Done().
            using Coroutine = std::function<Wait()>;

            // Run fn once after `seconds`. Negative counts as 0. A non finite value is refused and returns 0.
            TimerHandle After(double seconds, Callback fn, const std::shared_ptr<void>& owner = nullptr);

            // Run fn every `interval` seconds, first run after one interval. At most once per fixed step, a late timer does not replay missed runs.
            TimerHandle Every(double interval, Callback fn, const std::shared_ptr<void>& owner = nullptr);

            TimerHandle Start(Coroutine step, const std::shared_ptr<void>& owner = nullptr);

            // Safe from inside a callback, including cancelling the one that is running. False if the handle is unknown or already finished.
            bool Cancel(TimerHandle handle);
            // Only if the timer was made with that owner : what stops a plugin from touching another's by guessing a handle.
            bool Cancel(TimerHandle handle, const void* owner);
            void CancelOwned(const void* owner);
            void CancelAll();

            bool IsActive(TimerHandle handle) const;
            bool IsActive(TimerHandle handle, const void* owner) const;
            size_t Count() const;
            size_t CountOwned(const void* owner) const;

            // While one is alive on a thread, nothing can be scheduled from that thread (After, Every and Start return 0) : the world is not simulating (its edit tick),
            // so a timer made there would never run and would only pile up.
            class RefuseScope{
                public:
                    RefuseScope();
                    ~RefuseScope();
                    RefuseScope(const RefuseScope&) = delete;
                    RefuseScope& operator=(const RefuseScope&) = delete;
            };
            static bool Refusing();
            double Time() const { return time; }

            void Advance(double deltaSeconds);

        private:
            struct Entry{
                TimerHandle handle = 0;
                double wake = 0.0;
                std::function<bool()> until;
                Coroutine step;
                const void* ownerKey = nullptr;
                std::weak_ptr<void> owner;
                bool dead = false;
            };

            TimerHandle Add(double delay, Coroutine step, const std::shared_ptr<void>& owner);
            void Compact();
            Entry* Find(TimerHandle handle) const;

            // unique_ptr so a callback adding a timer never moves the entry that is executing.
            std::vector<std::unique_ptr<Entry>> entries;
            double time = 0.0;
            bool running = false;
    };

    class Sequence{
        public:
            Sequence& Do(Scheduler::Callback fn);
            Sequence& Delay(double seconds);
            Sequence& Until(std::function<bool()> pred, double timeout = std::numeric_limits<double>::infinity());
            Scheduler::Coroutine Build() const;

        private:
            struct Item{
                Scheduler::Callback fn;
                double delay = 0.0;
                std::function<bool()> until;
            };
            std::vector<Item> items;
    };
}
