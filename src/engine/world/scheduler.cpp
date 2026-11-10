#include "scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <string>

#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Worlds{

    namespace {
        // 0.5 s of 1/60 s steps sums to a hair under 0.5 in floating point, which would cost a whole step.
        constexpr double kEpsilon = 1e-9;

        thread_local int gRefuse = 0;

        // Shared by every scheduler, so a handle from a world that is gone never names a timer of the next one.
        TimerHandle NextHandle()
        {
            static TimerHandle next = 1;
            return next++;
        }
    }

    TimerHandle Scheduler::Add(double delay, Coroutine step, const std::shared_ptr<void>& owner)
    {
        if(!step || !std::isfinite(delay) || Refusing())
            return 0;

        auto entry = std::make_unique<Entry>();
        entry->handle = NextHandle();
        entry->wake = time + std::max(delay, 0.0);
        entry->step = std::move(step);
        entry->owner = owner;
        entry->ownerKey = owner.get();
        entries.push_back(std::move(entry));
        return entries.back()->handle;
    }

    TimerHandle Scheduler::After(double seconds, Callback fn, const std::shared_ptr<void>& owner)
    {
        if(!fn)
            return 0;
        return Add(seconds, [fn = std::move(fn)]{ fn(); return Wait::Done(); }, owner);
    }

    TimerHandle Scheduler::Every(double interval, Callback fn, const std::shared_ptr<void>& owner)
    {
        if(!fn || !std::isfinite(interval))
            return 0;
        interval = std::max(interval, 0.0);
        return Add(interval, [fn = std::move(fn), interval]{ fn(); return Wait::Seconds(interval); }, owner);
    }

    TimerHandle Scheduler::Start(Coroutine step, const std::shared_ptr<void>& owner)
    {
        return Add(0.0, std::move(step), owner);
    }

    Scheduler::Entry* Scheduler::Find(TimerHandle handle) const
    {
        for(auto& e : entries)
            if(e->handle == handle && !e->dead)
                return e.get();
        return nullptr;
    }

    bool Scheduler::Cancel(TimerHandle handle)
    {
        Entry* e = Find(handle);
        if(!e)
            return false;
        e->dead = true;
        Compact();
        return true;
    }

    bool Scheduler::Cancel(TimerHandle handle, const void* owner)
    {
        Entry* e = Find(handle);
        if(!e || !owner || e->ownerKey != owner)
            return false;
        return Cancel(handle);
    }

    void Scheduler::CancelOwned(const void* owner)
    {
        if(!owner)
            return;
        for(auto& e : entries)
            if(e->ownerKey == owner)
                e->dead = true;
        Compact();
    }

    void Scheduler::CancelAll()
    {
        for(auto& e : entries)
            e->dead = true;
        Compact();
    }

    bool Scheduler::IsActive(TimerHandle handle) const
    {
        return Find(handle) != nullptr;
    }

    bool Scheduler::IsActive(TimerHandle handle, const void* owner) const
    {
        const Entry* e = Find(handle);
        return e && owner && e->ownerKey == owner;
    }

    Scheduler::RefuseScope::RefuseScope() { gRefuse++; }
    Scheduler::RefuseScope::~RefuseScope() { gRefuse--; }
    bool Scheduler::Refusing() { return gRefuse > 0; }

    size_t Scheduler::CountOwned(const void* owner) const
    {
        return size_t(std::count_if(entries.begin(), entries.end(), [owner](const auto& e){ return !e->dead && e->ownerKey == owner; }));
    }

    size_t Scheduler::Count() const
    {
        return size_t(std::count_if(entries.begin(), entries.end(), [](const auto& e){ return !e->dead; }));
    }

    // Entries die in place while a callback runs, the vector is only touched once the pass is over.
    void Scheduler::Compact()
    {
        if(running)
            return;
        entries.erase(std::remove_if(entries.begin(), entries.end(), [](const auto& e){ return e->dead; }), entries.end());
    }

    void Scheduler::Advance(double deltaSeconds)
    {
        if(running || !(deltaSeconds > 0.0) || !std::isfinite(deltaSeconds))
            return;

        time += deltaSeconds;
        running = true;

        // Timers added by a callback have an index past this and wait for the next step.
        const size_t count = entries.size();
        for(size_t i = 0; i < count; i++){
            Entry& e = *entries[i];
            if(e.dead)
                continue;
            if(e.ownerKey && e.owner.expired()){
                e.dead = true;
                continue;
            }

            Wait next = Wait::Done();
            bool waited = false;
            try{
                // A throwing predicate cancels the timer like a throwing step.
                waited = bool(e.until);
                const bool due = e.until ? (e.until() || e.wake <= time + kEpsilon) : e.wake <= time + kEpsilon;
                if(!due)
                    continue;
                next = e.step();
            }catch(const std::exception& ex){
                DEBUG_WARNING(std::string("A timer threw and was cancelled : ") + ex.what());
            }catch(...){
                DEBUG_WARNING("A timer threw and was cancelled");
            }

            if(e.dead)
                continue;
            e.until = std::move(next.until);
            if(e.until){
                // A timeout of infinity or NaN waits for the predicate alone.
                e.wake = next.seconds >= 0.0 && std::isfinite(next.seconds) ? time + next.seconds : std::numeric_limits<double>::infinity();
                continue;
            }
            if(next.done || !std::isfinite(next.seconds)){
                e.dead = true;
                continue;
            }
            // Anchored on the previous wake so a repeating timer does not drift, never in the past so it cannot burst.
            // After a wait on a predicate the old wake is meaningless (infinite or the timeout), so anchor on now.
            e.wake = std::max((waited ? time : e.wake) + std::max(next.seconds, 0.0), time);
        }

        running = false;
        Compact();
    }

    Sequence& Sequence::Do(Scheduler::Callback fn)
    {
        if(fn)
            items.push_back({ std::move(fn), 0.0 });
        return *this;
    }

    Sequence& Sequence::Delay(double seconds)
    {
        items.push_back({ nullptr, seconds });
        return *this;
    }

    Sequence& Sequence::Until(std::function<bool()> pred, double timeout)
    {
        if(pred)
            items.push_back({ nullptr, timeout, std::move(pred) });
        return *this;
    }

    Scheduler::Coroutine Sequence::Build() const
    {
        auto shared = std::make_shared<const std::vector<Item>>(items);
        auto index = std::make_shared<size_t>(0);
        return [shared, index]() -> Wait{
            while(*index < shared->size()){
                const Item& item = (*shared)[(*index)++];
                if(item.fn)
                    item.fn();
                else if(item.until)
                    return Wait::Until(item.until, item.delay);
                else
                    return Wait::Seconds(item.delay);
            }
            return Wait::Done();
        };
    }
}
