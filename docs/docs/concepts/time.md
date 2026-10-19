# Time

How the engine measures time, and how a script should use it so a game behaves the same at 30 and at 300 FPS.

Everything lives in `Time::TimeManager` (`engine/world/time_manager.hpp`), reached with `Core::GetEngine().GetTimeManager()`. The engine ticks it once at the start of every frame.

## Two clocks

A frame runs with two kinds of time step :

| Step | Length | Used by |
|---|---|---|
| Variable (`GetDeltaTime()`) | what the previous frame took, in seconds | `Update` phase : scripts' `OnTick`, animation, audio |
| Fixed (`GetFixedDeltaTime()`) | constant, `physics.fixedTimeStep` in the project settings (1/60 s by default) | `Fixed` phase : scripts' `OnFixedTick`, physics and gameplay that must not depend on the frame rate |

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

## Script ticks

| Callback | Phase | Runs | Step |
|---|---|---|---|
| `OnFixedTick()` | `Fixed`, right before the physics step | 0..N times per frame | `GetFixedDeltaTime()` |
| `OnTick()` | `Update` | once per frame | `GetDeltaTime()` |
| `OnLateTick()` | `Late`, after the physics results are back | once per frame | `GetDeltaTime()` |

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

## Waiting

Script callbacks run on the main thread, inside the frame. **Never block in one** (`std::this_thread::sleep_for`, `PreciseSleep`, `future.wait()`) : the whole game, the renderer included, stops until it returns. "Waiting" in a script means *remembering that something is pending and checking again next tick*.

### Wait for a duration

Keep a countdown in the script and decrement it each tick. For anything that decides gameplay, count in the `Fixed` phase so the delay lasts the same number of simulation steps on every machine :

```cpp
void Door::OnFixedTick()
{
    if (m_CloseDelay <= 0.0f)
        return;

    m_CloseDelay -= Core::GetEngine().GetTimeManager()->GetFixedDeltaTime();
    if (m_CloseDelay <= 0.0f)
        Close();
}

void Door::OnOpened() { m_CloseDelay = 3.0f; }   // close 3 s of game time later
```

Counting in `OnFixedTick` also follows `timescale` (slow motion makes the delay longer, a freeze pauses it). A purely cosmetic delay can count `GetDeltaTime()` in `OnTick` instead, which stays in real time.

### Wait for a condition

Same idea, with a state instead of a number : test it each tick and act when it becomes true.

```cpp
void Loader::OnTick()
{
    if (m_State == State::WaitingForWorld && world->IsLoaded())
    {
        m_State = State::Ready;
        Begin();
    }
}
```

When a script has several steps in sequence (wait, then do A, wait, then do B), write them as an `enum class` state plus a timer. There are no coroutines : a callback always returns.

### Wait for a point in the frame

To act "after the physics", use `OnLateTick` rather than a flag checked next frame. To act on the next frame, set a boolean and test it at the top of `OnTick`.

## Running async code

For work that is too long for a frame (decoding, a path search, building a mesh, reading a file), move it off the main thread and **collect the result in a later tick**. Two tools exist.

### Job system

`Core::GetEngine().GetJobSystem()` is a work-stealing pool. Submit a closure into a `JobGroup`, keep the group, and poll `IsDone()` each tick instead of calling `Wait` :

```cpp
void Terrain::OnPlay()
{
    m_Job = std::make_unique<Core::JobGroup>();
    Core::GetEngine().GetJobSystem()->Submit(*m_Job, [this] {
        m_Heights = ComputeHeights(m_Seed);          // pure computation, no engine access
    });
}

void Terrain::OnTick()
{
    if (m_Job && m_Job->IsDone())                    // the result is ready : use it here, on the main thread
    {
        m_Job.reset();
        BuildMesh(m_Heights);
    }
}
```

`JobSystem::Wait(group)` blocks until the group is done. It is meant for code that has to join its own jobs inside one call (a parallel loop), not for "wait until the job finishes" across frames : in a script callback that is the same stall as a sleep. Do not destroy a `JobGroup` (or whatever the closure captures by reference) while jobs are still running ; if the script can be removed meanwhile, stop or finish the jobs in `OnStop` / the destructor.

### File IO

`Platform::AsyncIO::ReadFile` / `WriteFile` return a `std::future<IOResult>`. Poll it with a zero timeout :

```cpp
if (m_Read.valid() && m_Read.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
{
    IOResult result = m_Read.get();
    if (result.ok)
        Parse(result.data);
}
```

### Rules for async code

* **Compute off-thread, apply on the main thread.** A job must not touch the world, components, the renderer or the physics : none of them is thread-safe. Let the job fill data that only it owns, and apply it from `OnTick` once it is done.
* A job must not throw, and must not block on something only another job will provide.
* Capture by value what the job needs. A raw `this` is only safe if the script outlives the job.
* The result arrives a frame or more later : the script has to cope with "not ready yet" (a flag, a state, `IsDone()`).
* Async work does not follow the time scale or the fixed step. If the outcome must be reproducible (replays, [deterministic runs](headless.md)), do not let gameplay depend on *which frame* a job finishes : wait for it, or apply it at a fixed point.

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
