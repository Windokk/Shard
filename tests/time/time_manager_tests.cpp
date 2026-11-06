#include <gtest/gtest.h>

#include "engine/world/time_manager.hpp"

using Shard::Engine::Time::TimeManager;

namespace {

    float SimulatedTimeOver(TimeManager& time, int frames, float frameDelta)
    {
        int steps = 0;
        for (int i = 0; i < frames; i++)
        {
            time.Advance(frameDelta);
            steps += time.ConsumeFixedSteps();
        }
        return steps * time.GetFixedDeltaTime();
    }
}

TEST(TimeManager, SimTimeMatchesRealTime)
{
    const float fixedStep = 1.0f / 30.0f;
    const float realSeconds = 2.0f;

    const float framerates[] = { 500.0f, 60.0f, 20.0f };

    for (float fps : framerates)
    {
        TimeManager time;
        time.Init(fixedStep);

        const int frames = static_cast<int>(fps * realSeconds);
        const float simulated = SimulatedTimeOver(time, frames, 1.0f / fps);

        // At most one step of rounding error: the remainder stays in the accumulator and is paid out later.
        EXPECT_NEAR(simulated, realSeconds, fixedStep) << "at " << fps << " fps";
    }
}

TEST(TimeManager, LeftoverCarriesOver)
{
    TimeManager time;
    time.Init(1.0f / 30.0f);

    time.Advance((1.0f / 30.0f) * (2.0f / 3.0f));
    EXPECT_EQ(time.ConsumeFixedSteps(), 0);

    time.Advance((1.0f / 30.0f) * (2.0f / 3.0f));
    EXPECT_EQ(time.ConsumeFixedSteps(), 1);
}

TEST(TimeManager, LongFrameClampedAgainstSpiral)
{
    const float fixedStep = 1.0f / 30.0f;
    const float maxAccumulated = 0.25f;

    TimeManager time;
    time.Init(fixedStep, maxAccumulated);

    // A one-second stall must not hand out a second's worth of steps.
    time.Advance(1.0f);

    const int steps = time.ConsumeFixedSteps();
    EXPECT_GT(steps, 1);
    EXPECT_LE(steps * fixedStep, maxAccumulated);
}

TEST(TimeManager, SpeedScalesSimNotReal)
{
    const float fixedStep = 1.0f / 30.0f;

    TimeManager time;
    time.Init(fixedStep);
    time.SetTimeSpeed(0.5f);

    const float simulated = SimulatedTimeOver(time, 120, 1.0f / 60.0f);

    EXPECT_NEAR(simulated, 1.0f, fixedStep);
    EXPECT_NEAR(time.GetDeltaTime(), 1.0f / 60.0f, 1e-6f);
}

TEST(TimeManager, ResetAccumulatorDropsBankedTime)
{
    TimeManager time;
    time.Init(1.0f / 30.0f);

    time.Advance(0.2f);
    time.ResetAccumulator();

    EXPECT_EQ(time.ConsumeFixedSteps(), 0);
}
