# Large world

How Shard keeps positions exact from a room to a solar system.

## The problem

A `float` has 24 bits of mantissa : 10 km from the origin the smallest step is ~1 mm, at 100 km it is ~8 mm and things jitter. A `double` is exact to a micrometre over the solar system, but the GPU wants floats. The rule is :

> **Big numbers live in the frames' origins and in integers. Everything else is small.**

## The pieces

| Piece | Where | What it is |
|---|---|---|
| `WorldPosition` | `core/math/large_world.hpp` | `(CellCoord cell, dvec3 local)` : an `int64` cell of 1024 m plus a double offset inside it. `a - b` is exact when `a` and `b` are near each other, however far from the origin they are. |
| `ReferenceFrameTree` | `core/math/reference_frame.hpp` | Nested frames (floating origins : a planet, a ship, a streaming cell). Each frame has an origin (`WorldPosition`) and a rotation in its parent. Converts points, directions and rotations between any two frames, through their nearest common ancestor. |
| `World::GetFrames()` | `world/world.hpp` | Every world owns a tree. It is saved in the world file (`"frames"`). |
| `Transform::frame` | `world/components/transform.hpp` | The frame an actor hierarchy lives in (set on the root actor). `position` stays a small float *in that frame*. |
| Jolt in double | `CMakeLists.txt` | `SHARD_DOUBLE_PRECISION_PHYSICS` (ON) builds Jolt with `JPH_DOUBLE_PRECISION` : bodies are positioned in the root frame in double. |
| Extract | `renderer/components/render_extractor.cpp` | Turns everything into camera-relative floats. The renderer is unchanged. |

## Using it

```cpp
auto& frames = world.GetFrames();
FrameId planet = frames.Create("Planet", kRootFrame,
                               WorldPosition(CellCoord{1000000000, 0, 0}, dvec3(0)));   // ~10^12 m from the root

actor->transform->frame = planet;               // a root actor of the hierarchy
actor->transform->SetPosition({0, 2, 0});       // small, in the planet's frame

WorldPosition where = actor->transform->GetWorldCoords();   // exact, in the root frame
dvec3 p = actor->transform->GetWorldPositionD();            // the same, collapsed to a double
actor->transform->SetWorldCoords(frames.ToRoot(planet, dvec3(10, 0, -4)));

dvec3 q = frames.ConvertPoint(shipFrame, planetFrame, dvec3(1, 2, 3));   // frame -> frame
```

`ConvertPoint` between two frames that are far from the root but near each other keeps the precision of their small distance : the two legs of the conversion are carried as `WorldPosition`s and only the exact difference is narrowed.

## Physics

Jolt simulates in the root frame, in double. A `PhysicsBody` is created at `Transform::GetWorldPositionD()` / `GetWorldRotationD()` and, after every step, its result is brought back into the actor's frame (`RootToFramePosition`), so the transform, the pose interpolation and the world matrix keep working with small floats. Velocities, sizes and forces stay float. `PhysicsBody::GetPositionD` / `SetPositionD` are the double accessors.

Frames are assumed static while their bodies are simulated (moving a frame does not move the bodies already in Jolt : teleport them with `SetPositionD`).

Changing the Jolt precision changes the simulation : `tests/determinism/golden/determinism.replay` was recorded again with this change.

## Rendering

The Extract stage picks a **render origin**, a `WorldPosition` of the root frame that follows the camera. Every position, matrix and view put in the `RenderSnapshot` is a float relative to it (`FrameToRender`, `render_space.hpp` : the subtraction is done in double, on exact positions, before the narrowing). The renderer only sees small numbers and does not know any of this.

* The origin moves (to a cell corner under the camera) only when the camera is farther than `render.rebase_distance` from it (default 4096 m, `0` = always exactly at the camera). In a scene that fits within that distance the origin never moves, render space *is* the root frame, and extraction gives the very same matrices as before (bit for bit).
* When it moves, `RenderSnapshot::originShift` is where the previous origin is seen from the new one : something that stores a render-space position from one frame to the next (temporal history) adds it. Every proxy is flagged `changed` that frame.
* Not yet covered : the orientation of probe volumes in a rotated frame (their boxes stay axis-aligned), and the editor's picking / gizmos, which still work in the actor's frame (identical to the render space as long as the origin has not moved).
