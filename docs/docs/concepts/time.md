# Time

How the engine measures time, and how a script should use it so a game behaves the same at 30 and at 300 FPS.

Everything lives in `Time::TimeManager` (`engine/world/time_manager.hpp`), reached with `Core::GetEngine().GetTimeManager()`. The engine ticks it once at the start of every frame.

## Two clocks

A frame runs with two kinds of time step :

| Step | Length | Used by |
|---|---|---|
| Variable (`GetDeltaTime()`) | what the previous frame took, in seconds | `Update` phase : scripts' `OnTick`, animation, audio, camera |
| Fixed (`GetFixedDeltaTime()`) | constant, `physics.fixedTimeStep` in the project settings (1/60 s by default) | `Fixed` phase : physics and gameplay that must not depend on the frame rate |

The frame goes through the scheduler phases `Input → PreSim → Fixed × N → PostPhysics → Update → Late → Extract`. `Fixed` runs *N* times, `N` being the number of whole fixed steps owed this frame ; every other phase runs once.

## The accumulator

Real time is banked into an accumulator, and paid out as whole fixed steps :

```
accumulator += min(deltaTime * timeSpeed, maxAccumulatedTime)
steps       = floor(accumulator / fixedDeltaTime)     // ConsumeFixedSteps()
accumulator -= steps * fixedDeltaTime
```

So at 120 FPS about every other frame runs no step, at 30 FPS every frame runs two, and the simulation covers the same game time either way.

`maxAccumulatedTime` (`physics.maxAccumulatedTime`, 0.25 s by default) caps what one frame can bank. A frame that stalls (a breakpoint, a world load, a window drag) is clamped instead of owed in full, otherwise the next frame would have to run so many steps that it stalls too, and so on : the spiral of death. The price is that the game slows down instead of catching up.

The accumulator is emptied while the editor is not in play mode, so pressing Play does not replay a burst of steps.

### Smoothing the render

What is left in the accumulator is how far the frame sits between two simulated states. `GetFixedStepAlpha()` gives it in `[0, 1)` ; the renderer uses it to interpolate transforms, so motion stays smooth when the render rate is not a multiple of the simulation rate.

## Which one to use

* Moving something in `OnTick` : multiply by `GetDeltaTime()`.
* Anything that must be reproducible (forces, timers that decide gameplay) : do it in the `Fixed` phase and use `GetFixedDeltaTime()`.
* Never accumulate `GetDeltaTime()` into a timer that decides gameplay : it differs from machine to machine.

```cpp
void Character::OnTick()
{
    const float dt = Core::GetEngine().GetTimeManager()->GetDeltaTime();
    position += velocity * dt;
}
```

## Time scale

`SetTimeSpeed(x)` scales how fast game time is banked : `0.5` is slow motion (half as many fixed steps per second), `0` freezes the simulation, `2` doubles it. In the console :

```
timescale          # prints the current value
timescale 0.25     # between 0 and 100
```

!!! note
    The scale changes the *number* of fixed steps, not their length : physics stays stable in slow motion. It does not scale `GetDeltaTime()`, so variable-step code (`OnTick`) keeps running in real time. A script that should follow the scale multiplies by `GetTimeSpeed()` itself.

## Wall-clock time

| Call | Returns |
|---|---|
| `CurrentAppTime()` | time since the engine started, as a `TimeStamp` (monotonic `steady_clock`) |
| `CurrentGlobalTime()` | the local date and time of day (through the platform layer), `days` being the day of the year |

`TimeStamp` splits a duration into `days / hours / minutes / seconds / milliseconds / nanoseconds`. `Normalize()` carries overflows up (`1500 ms` becomes `1 s 500 ms`) and `AsString()` prints it as `0d 00h:01m:05s:250ms:000000ns`.

## Deterministic runs

With `EngineCreationSettings::deterministic` (always in `ShardHeadless`, and with `--record` / `--replay`), the clock is not read : every frame lasts exactly one fixed step, so exactly one step is run per frame, whatever the machine is doing. See [Headless mode and determinism](headless.md).

To get the same without a full deterministic run (a benchmark, a test), set the `time.frame_dt` variable to a length in seconds : every frame then lasts that long. `0` (the default) is real time.

## Settings reference

| Setting | Where | Default | Effect |
|---|---|---|---|
| `physics.fixedTimeStep` | project settings | 1/60 | length of a fixed step |
| `physics.maxAccumulatedTime` | project settings | 0.25 | most real time one frame may bank |
| `time.frame_dt` | variable | 0 | force the length of every frame |
| `timescale` | console command | 1 | speed of the simulation |
| `engine.maxFPS` | variable | 0 | frame rate cap (0 : none) |
