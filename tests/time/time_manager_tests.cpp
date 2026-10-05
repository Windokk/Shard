#include <gtest/gtest.h>

#include "engine/world/time_manager.hpp"

using Shard::Engine::Time::TimeManager;

namespace {

    /// Runs `frames` frames of `frameDelta` seconds each and returns how much simulated time the
    /// fixed-step loop handed out, which is what the simulation actually advances by.
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

TEST(TimeManager, SimulatedTimeMatchesRealTimeWhateverTheFramerate)
{
    const float fixedStep = 1.0f / 30.0f;
    const float realSeconds = 2.0f;

    // 500 fps, 60 fps and 20 fps - one far above the simulation rate, one a multiple of it, one below.
    const float framerates[] = { 500.0f, 60.0f, 20.0f };

    for (float fps : framerates)
    {
        TimeManager time;
        time.Init(fixedStep);

        const int frames = static_cast<int>(fps * realSeconds);
        const float simulated = SimulatedTimeOver(time, frames, 1.0f / fps);

        // Never more than one step of rounding out, whatever the framerate: the remainder stays in the
        // accumulator and is paid out on a later frame rather than being lost or double counted.
        EXPECT_NEAR(simulated, realSeconds, fixedStep) << "at " << fps << " fps";
    }
}

TEST(TimeManager, LeftoverTimeIsCarriedToTheNextFrame)
{
    TimeManager time;
    time.Init(1.0f / 30.0f);

    // Two thirds of a step: not enough to run one yet.
    time.Advance((1.0f / 30.0f) * (2.0f / 3.0f));
    EXPECT_EQ(time.ConsumeFixedSteps(), 0);

    // The leftover plus this frame is more than a step, so exactly one comes out.
    time.Advance((1.0f / 30.0f) * (2.0f / 3.0f));
    EXPECT_EQ(time.ConsumeFixedSteps(), 1);
}

TEST(TimeManager, LongFrameRunsSeveralStepsButIsClampedAgainstTheSpiralOfDeath)
{
    const float fixedStep = 1.0f / 30.0f;
    const float maxAccumulated = 0.25f;

    TimeManager time;
    time.Init(fixedStep, maxAccumulated);

    // A frame that took a whole second (a stall) must not hand out a second's worth of steps.
    time.Advance(1.0f);

    const int steps = time.ConsumeFixedSteps();
    EXPECT_GT(steps, 1);
    EXPECT_LE(steps * fixedStep, maxAccumulated);
}

TEST(TimeManager, TimeSpeedScalesTheSimulationButNotRealTime)
{
    const float fixedStep = 1.0f / 30.0f;

    TimeManager time;
    time.Init(fixedStep);
    time.SetTimeSpeed(0.5f);

    const float simulated = SimulatedTimeOver(time, 120, 1.0f / 60.0f); // 2 real seconds

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
