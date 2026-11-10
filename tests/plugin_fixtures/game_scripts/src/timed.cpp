#include <shard/game.hpp>

#include "stats.hpp"

// Delays, repeating timers and coroutines of a script (shard/cpp/timers.hpp). The test steps the world and reads the counters.
class Timed : public Shard::Script {
public:
    void OnPlay() override
    {
        After(0.5, [] { ++GetStats().timerOnce; });
        every = Every(0.25, [] { ++GetStats().timerEvery; });

        StartCoroutine([state = 0]() mutable -> Shard::Wait {
            switch (state++) {
                case 0: ++GetStats().coroSteps; return Shard::Wait::Seconds(1.0);
                case 1: ++GetStats().coroSteps; return Shard::Wait::Until([] { return GetStats().untilFlag != 0; }, 5.0);
                default: ++GetStats().coroDone; return Shard::Wait::Done();
            }
        });

        go = Shard::EventLatch("timed.go");
        StartCoroutine(Shard::Sequence().UntilEvent(go, 3.0).Do([this] { GetStats().eventFired = go.Fired() ? 1 : 0; ++GetStats().eventResumed; }).Build());
    }

    void StopEvery() { if (StopTimer(every)) ++GetStats().stoppedEvery; }

private:
    ShardTimer every = 0;
    Shard::EventLatch go;
};
SHARD_REGISTER_SCRIPT(Timed)
