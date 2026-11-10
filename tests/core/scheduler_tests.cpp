#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include "engine/world/scheduler.hpp"
#include "engine/world/scheduler_events.hpp"

using namespace Shard::Engine::Worlds;

namespace {
    constexpr double kStep = 1.0 / 60.0;

    void Steps(Scheduler& s, int steps)
    {
        for (int i = 0; i < steps; i++)
            s.Advance(kStep);
    }
}

TEST(Scheduler, AfterFiresOnceOnTheRightStep)
{
    Scheduler s;
    int calls = 0;
    s.After(0.5, [&]{ calls++; });

    Steps(s, 29);
    EXPECT_EQ(calls, 0);
    Steps(s, 1);
    EXPECT_EQ(calls, 1);
    Steps(s, 120);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(s.Count(), 0u);
}

TEST(Scheduler, EveryRepeatsWithoutDrift)
{
    Scheduler s;
    int calls = 0;
    s.Every(0.25, [&]{ calls++; });

    Steps(s, 60 * 10);
    EXPECT_EQ(calls, 40);
}

TEST(Scheduler, EveryShorterThanAStepRunsOncePerStep)
{
    Scheduler s;
    int calls = 0;
    s.Every(0.0, [&]{ calls++; });

    Steps(s, 10);
    EXPECT_EQ(calls, 10);
}

TEST(Scheduler, CancelBeforeFire)
{
    Scheduler s;
    int calls = 0;
    const TimerHandle h = s.After(0.1, [&]{ calls++; });
    EXPECT_TRUE(s.IsActive(h));
    EXPECT_TRUE(s.Cancel(h));
    EXPECT_FALSE(s.Cancel(h));
    EXPECT_FALSE(s.IsActive(h));

    Steps(s, 60);
    EXPECT_EQ(calls, 0);
}

TEST(Scheduler, CallbackCancelsItselfAndOthers)
{
    Scheduler s;
    int a = 0, b = 0;
    TimerHandle hb = 0;
    TimerHandle ha = s.Every(0.0, [&]{ a++; s.Cancel(ha); s.Cancel(hb); });
    hb = s.Every(0.0, [&]{ b++; });

    Steps(s, 5);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 0);
    EXPECT_EQ(s.Count(), 0u);
}

TEST(Scheduler, CallbackAddingATimerWaitsForTheNextStep)
{
    Scheduler s;
    std::vector<int> order;
    s.After(0.0, [&]{
        order.push_back(1);
        for (int i = 0; i < 100; i++)   // grows the vector while an entry executes
            s.After(0.0, [&]{ order.push_back(2); });
    });

    s.Advance(kStep);
    EXPECT_EQ(order.size(), 1u);
    s.Advance(kStep);
    EXPECT_EQ(order.size(), 101u);
}

TEST(Scheduler, CancelAllFromACallback)
{
    Scheduler s;
    int calls = 0;
    s.After(0.0, [&]{ calls++; s.CancelAll(); });
    s.After(0.0, [&]{ calls++; });

    Steps(s, 3);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(s.Count(), 0u);
}

TEST(Scheduler, CoroutineWaitsAndFinishes)
{
    Scheduler s;
    std::vector<double> at;
    int state = 0;
    s.Start([&]() -> Wait{
        at.push_back(s.Time());
        if (state++ < 2)
            return Wait::Seconds(1.0);
        return Wait::Done();
    });

    Steps(s, 60 * 3 + 5);
    ASSERT_EQ(at.size(), 3u);
    EXPECT_NEAR(at[1] - at[0], 1.0, kStep);
    EXPECT_NEAR(at[2] - at[1], 1.0, kStep);
    EXPECT_EQ(s.Count(), 0u);
}

TEST(Scheduler, NextStepRunsEveryStep)
{
    Scheduler s;
    int calls = 0;
    s.Start([&]() -> Wait{ return ++calls < 4 ? Wait::NextStep() : Wait::Done(); });

    Steps(s, 10);
    EXPECT_EQ(calls, 4);
}

TEST(Scheduler, SequenceRunsInOrder)
{
    Scheduler s;
    std::vector<int> order;
    Sequence seq;
    seq.Do([&]{ order.push_back(1); }).Delay(1.0).Do([&]{ order.push_back(2); }).Do([&]{ order.push_back(3); });
    s.Start(seq.Build());

    Steps(s, 30);
    EXPECT_EQ(order, (std::vector<int>{ 1 }));
    Steps(s, 40);
    EXPECT_EQ(order, (std::vector<int>{ 1, 2, 3 }));
    EXPECT_EQ(s.Count(), 0u);
}

TEST(Scheduler, OwnerDeathCancels)
{
    Scheduler s;
    int calls = 0;
    auto owner = std::make_shared<int>(0);
    s.Every(0.0, [&]{ calls++; }, owner);

    Steps(s, 3);
    EXPECT_EQ(calls, 3);
    owner.reset();
    Steps(s, 3);
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(s.Count(), 0u);
}

TEST(Scheduler, CancelOwnedOnlyTouchesThatOwner)
{
    Scheduler s;
    int a = 0, b = 0;
    auto ownerA = std::make_shared<int>(0);
    auto ownerB = std::make_shared<int>(0);
    s.Every(0.0, [&]{ a++; }, ownerA);
    s.Every(0.0, [&]{ b++; }, ownerB);
    s.Every(0.0, []{});

    s.CancelOwned(ownerA.get());
    Steps(s, 2);
    EXPECT_EQ(a, 0);
    EXPECT_EQ(b, 2);
    EXPECT_EQ(s.Count(), 2u);
}

TEST(Scheduler, ThrowingCallbackIsCancelledAndOthersStillRun)
{
    Scheduler s;
    int ok = 0;
    s.Every(0.0, []{ throw std::runtime_error("boom"); });
    s.Every(0.0, [&]{ ok++; });

    Steps(s, 3);
    EXPECT_EQ(ok, 3);
    EXPECT_EQ(s.Count(), 1u);
}

TEST(Scheduler, InvalidArguments)
{
    Scheduler s;
    EXPECT_EQ(s.After(0.1, nullptr), 0u);
    EXPECT_EQ(s.After(std::numeric_limits<double>::infinity(), []{}), 0u);
    EXPECT_EQ(s.After(std::numeric_limits<double>::quiet_NaN(), []{}), 0u);
    EXPECT_EQ(s.Every(std::numeric_limits<double>::quiet_NaN(), []{}), 0u);
    EXPECT_EQ(s.Start(nullptr), 0u);

    int calls = 0;
    s.After(-5.0, [&]{ calls++; }); 
    s.Advance(-1.0);
    s.Advance(std::numeric_limits<double>::quiet_NaN());
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(s.Time(), 0.0);
    s.Advance(kStep);
    EXPECT_EQ(calls, 1);
}

TEST(Scheduler, UntilResumesWhenThePredicateTurnsTrue)
{
    Scheduler s;
    bool flag = false;
    int resumed = 0;
    int state = 0;
    s.Start([&]() -> Wait{
        if (state++ == 0)
            return Wait::Until([&]{ return flag; });
        resumed++;
        return Wait::Done();
    });

    Steps(s, 600);
    EXPECT_EQ(resumed, 0);
    EXPECT_EQ(s.Count(), 1u);
    flag = true;
    Steps(s, 1);
    EXPECT_EQ(resumed, 1);
    EXPECT_EQ(s.Count(), 0u);
}

TEST(Scheduler, UntilTimesOut)
{
    Scheduler s;
    double resumedAt = -1.0;
    int state = 0;
    s.Start([&]() -> Wait{
        if (state++ == 0)
            return Wait::Until([]{ return false; }, 2.0);
        resumedAt = s.Time();
        return Wait::Done();
    });

    Steps(s, 60);
    EXPECT_LT(resumedAt, 0.0);
    Steps(s, 61);
    EXPECT_NEAR(resumedAt, 2.0, 2 * kStep);
}

TEST(Scheduler, WaitAfterAnUntilStillWorks)
{
    // The wake time left by an infinite wait must not freeze the next Seconds wait.
    Scheduler s;
    bool flag = true;
    std::vector<double> at;
    int state = 0;
    s.Start([&]() -> Wait{
        at.push_back(s.Time());
        switch (state++) {
            case 0: return Wait::Until([&]{ return flag; });
            case 1: return Wait::Seconds(1.0);
            default: return Wait::Done();
        }
    });

    Steps(s, 120);
    ASSERT_EQ(at.size(), 3u);
    EXPECT_NEAR(at[2] - at[1], 1.0, kStep);
    EXPECT_EQ(s.Count(), 0u);
}

TEST(Scheduler, ThrowingPredicateCancels)
{
    Scheduler s;
    int state = 0;
    s.Start([&]() -> Wait{ return state++ == 0 ? Wait::Until([]() -> bool { throw std::runtime_error("boom"); }) : Wait::Done(); });

    Steps(s, 3);
    EXPECT_EQ(s.Count(), 0u);
}

TEST(Scheduler, SequenceUntil)
{
    Scheduler s;
    bool flag = false;
    std::vector<int> order;
    s.Start(Sequence().Do([&]{ order.push_back(1); }).Until([&]{ return flag; }).Do([&]{ order.push_back(2); }).Build());

    Steps(s, 10);
    EXPECT_EQ(order, (std::vector<int>{ 1 }));
    flag = true;
    Steps(s, 2);
    EXPECT_EQ(order, (std::vector<int>{ 1, 2 }));
}

namespace {
    using Shard::Engine::Core::ObjectID;
    using Shard::Engine::Events::EventDispatcher;
    using Shard::Engine::Events::KeyPressedEvent;
}

TEST(EventWait, FiresOnceAndUnsubscribes)
{
    EventDispatcher d;
    auto wait = EventWait<KeyPressedEvent>::Global(d);
    EXPECT_FALSE(wait.Fired());

    d.emitGlobal(KeyPressedEvent(1, false, ObjectID(1)));
    EXPECT_TRUE(wait.Fired());
    d.emitGlobal(KeyPressedEvent(2, false, ObjectID(1)));
    EXPECT_TRUE(wait.Fired());
}

TEST(EventWait, FilterAndActorScope)
{
    EventDispatcher d;
    auto wait = EventWait<KeyPressedEvent>::ForActor(d, ObjectID(7), [](const KeyPressedEvent& e){ return e.keyCode == 5; });

    d.emitGlobal(KeyPressedEvent(5, false, ObjectID(7)));
    d.emitToActor(ObjectID(8), KeyPressedEvent(5, false, ObjectID(8)));
    d.emitToActor(ObjectID(7), KeyPressedEvent(4, false, ObjectID(7)));
    EXPECT_FALSE(wait.Fired());
    d.emitToActor(ObjectID(7), KeyPressedEvent(5, false, ObjectID(7)));
    EXPECT_TRUE(wait.Fired());
}

TEST(EventWait, DroppingTheWaitUnsubscribes)
{
    EventDispatcher d;
    {
        auto wait = EventWait<KeyPressedEvent>::Global(d);
        auto copy = wait;
    }
    d.emitGlobal(KeyPressedEvent(1, false, ObjectID(1)));   // must not touch the dead state
    SUCCEED();
}

TEST(EventWait, ResumesACoroutineWithTimeoutOrEvent)
{
    EventDispatcher d;
    Scheduler s;
    auto wait = EventWait<KeyPressedEvent>::Global(d);
    int resumed = 0;
    s.Start(Sequence().Until(wait.Predicate(), 10.0).Do([&]{ resumed++; }).Build());

    Steps(s, 5);
    EXPECT_EQ(resumed, 0);
    d.emitGlobal(KeyPressedEvent(1, false, ObjectID(1)));
    Steps(s, 2);
    EXPECT_EQ(resumed, 1);
    EXPECT_TRUE(wait.Fired());
}

TEST(EventWait, CancelledCoroutineReleasesTheSubscription)
{
    EventDispatcher d;
    Scheduler s;
    {
        auto wait = EventWait<KeyPressedEvent>::Global(d);
        s.Start(Sequence().Until(wait.Predicate()).Build());
    }
    Steps(s, 2);
    s.CancelAll();
    d.emitGlobal(KeyPressedEvent(1, false, ObjectID(1)));
    SUCCEED();
}

TEST(Scheduler, RefusesWhileAScopeIsAlive)
{
    Scheduler s;
    {
        Scheduler::RefuseScope refuse;
        EXPECT_TRUE(Scheduler::Refusing());
        EXPECT_EQ(s.After(0.1, []{}), 0u);
        EXPECT_EQ(s.Every(0.1, []{}), 0u);
        EXPECT_EQ(s.Start([]() -> Wait{ return Wait::Done(); }), 0u);
    }
    EXPECT_FALSE(Scheduler::Refusing());
    EXPECT_NE(s.After(0.1, []{}), 0u);
    EXPECT_EQ(s.Count(), 1u);
}

TEST(Scheduler, CountsOwnedTimers)
{
    Scheduler s;
    auto a = std::make_shared<int>(0);
    auto b = std::make_shared<int>(0);
    s.Every(1.0, []{}, a);
    s.Every(1.0, []{}, a);
    s.Every(1.0, []{}, b);
    s.Every(1.0, []{});
    EXPECT_EQ(s.CountOwned(a.get()), 2u);
    EXPECT_EQ(s.CountOwned(b.get()), 1u);
    s.CancelOwned(a.get());
    EXPECT_EQ(s.CountOwned(a.get()), 0u);
}
