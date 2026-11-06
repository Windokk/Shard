# Gravity fields

How to change which way, and how hard, bodies fall in a region of the world.

Jolt has one gravity for the whole simulation (`physics.gravity` in the project settings). Gravity fields and the per-body gravity are done on top of it : before every step, each awake dynamic body is given as a force the difference between the gravity it should feel and the one Jolt is about to give it. A project without fields, with every body on the default mode, pays nothing for it.

## The fields

All of them are components with a `strength` (acceleration, in world units per second squared, `9.81` for the Earth in a project in metres, negative pushes away), a `priority` and a `blend`.

| Component | Type name | What it does |
|---|---|---|
| `UniformGravityField` | `gravityUniform` | The same gravity everywhere : a `direction` (down by default) turned by the rotation of its actor. A level's own gravity. |
| `PointGravityField` | `gravityPoint` | Pulls toward its actor, with a `radius` (0 : unlimited) and a `falloff` : `Constant`, `Linear` (to nothing at the radius) or `InverseSquare` (`strength` is reached at `referenceDistance`, and stops growing under `minDistance`). |
| `RadialGravityField` | `gravityRadial` | A planet : `strength` is the gravity of the surface at `radius`. Above it falls by `falloff` (inverse square by default) up to `reach` (0 : unlimited). Inside, it decreases to the center like a ball of uniform density (`solidInterior`) or stays constant. |
| `GravityVolume` | `gravityVolume` | A region (the box of its actor, or the sphere / cylinder inscribed in it, following its position, rotation and scale) where gravity is something else. It points `Fixed` (along `direction`), `ToCenter` or `ToAxis` (the Y axis : a rotating station, with a negative strength). Fades in over `fadeDistance` inside its edge. |

Point, radial and volume fields fade to nothing at their edge when given a `fadeDistance`, so a body crossing it feels the change gradually.

## Combining them

The fields that cover a position are read from the highest `priority` to the lowest :

* an `Override` field replaces what is below it, over the share of the position it covers (1 deep inside, less near a fading edge) ;
* overriding fields of the same priority that overlap blend, by their coverage ;
* what no field covers is the lower fields', and at last the project's gravity ;
* an `Additive` field is summed on top of the result, whatever its priority (a moon pulling over a planet's gravity).

A body on the far side of a planet falls toward its center, and a body walking from a planet's field into a corridor volume of a higher priority sees its gravity turn over the corridor's fade.

## Per-body gravity

`PhysicsBody` has a `gravityMode` :

| Mode | The body feels |
|---|---|
| `Fields` (default) | the project's gravity, changed by the fields where its center of mass is |
| `World` | the project's gravity alone, the fields do not touch it |
| `Custom` | its own `customGravity` |

The `gravityFactor` scales whatever the gravity is (0 floats, negative falls up).

```cpp
auto body = actor->GetComponent<PhysicsBody>();

body->SetCustomGravity(glm::vec3(0, 3, 0));         // gravityMode = Custom
body->SetGravityMode(GravityMode::Fields);          // back to the world's

glm::vec3 g = body->GetGravity();                   // what it falls with now, gravity factor included
glm::vec3 up = -glm::normalize(g);                  // the "up" of a character standing on a planet
```

`PhysicsManager::GetGravityAt(position)` gives the gravity of the fields and the world at any point of the root frame, for what is not a body (a camera, a particle, an AI).

## Notes

* Fields are read in the root frame, in double, like Jolt : a planet 10^12 m from the origin works (see [Large world](large-world.md)).
* Fields are read again for every fixed step : move them, turn them, change their fields from a script.
* A sleeping body is woken when a change of the fields changes the gravity it would feel (a field appearing, going away, moving), and left alone otherwise. A body whose component is deactivated is never woken.
* The kinematic and static bodies ignore gravity, as always.
* The simulation does not depend on the order the components were added in : a tie between two fields of the same priority is broken by their order in the world.
* `PhysicsManager::SetGravityFields` takes the fields as plain numbers (`GravityFieldData`), for tests or for code that has no component to give.
