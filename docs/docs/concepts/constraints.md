# Constraints

How to join two actors, or an actor and the world: doors, wheels, ropes, drawers, ragdolls, chains.

A constraint is a component on an actor that has a [physics body](gravity.md). It joins that body to another one and limits how they move relative to each other. The pose the two bodies have when the game starts is the rest pose, so a constraint holds what you set up in the editor.

## Quick start

1. Give a door a physics body, set to dynamic.
2. Give the door frame a physics body, set to static, and tag it `Door.Frame`.
3. Add a Hinge constraint to the door. Set `target` to `Door.Frame`, `anchor` to the edge of the door, `axis` to up, and turn on `limited`.

Press play. The door swings around its edge and stops at the limits.

## Components

| Component | Type name | What it allows |
|---|---|---|
| `FixedConstraint` | `constraintFixed` | nothing, the two bodies move as one |
| `PointConstraint` | `constraintPoint` | any rotation around the pivot |
| `HingeConstraint` | `constraintHinge` | a turn around one axis |
| `SliderConstraint` | `constraintSlider` | a slide along one axis |
| `DistanceConstraint` | `constraintDistance` | any movement that keeps the distance in a range |
| `ConeConstraint` | `constraintCone` | a lean of an axis inside a cone |
| `SwingTwistConstraint` | `constraintSwingTwist` | a lean in a cone or pyramid, plus a twist around the axis |

A fixed constraint ignores `anchor`, the others pivot around it.

## Common fields

| Field | Meaning |
|---|---|
| `target` | tag query of the other actor. The first match that has a physics body is used. Empty joins the actor to the world, so it hangs in place |
| `anchor` | pivot, in the space of this actor |
| `collideConnected` | off by default, the two joined bodies do not collide with each other |
| `breakForce` | force in newtons above which the constraint breaks. 0 never breaks |
| `breakTorque` | torque in newton metres above which the constraint breaks. 0 never breaks |

A target that does not exist yet, for example an actor spawned later, is searched again every half second until it is found.

## Hinge

| Field | Meaning |
|---|---|
| `axis` | axis of the turn, in the space of the actor |
| `limited`, `minAngle`, `maxAngle` | limits in degrees, from the rest pose |
| `limitFrequency`, `limitDamping` | soft limits. A frequency above 0 makes the limit a spring |
| `friction` | torque that resists the turn |
| `motor` | `Off`, `Velocity` or `Position` |
| `motorTarget` | degrees per second, or degrees for a position motor |
| `motorMaxTorque` | strength of the motor |
| `motorFrequency`, `motorDamping` | stiffness of a position motor |

```cpp
auto hinge = door.GetComponent<HingeConstraint>();
hinge->SetMotor(ConstraintMotor::Position, 90.0f);
float angle = hinge->GetAngle();
```

## Slider

Same fields as the hinge, with distances instead of angles, `friction` as a force and `motorMaxForce`. `GetPosition()` gives the current position.

## Distance

| Field | Meaning |
|---|---|
| `targetAnchor` | where the other end is attached, in the space of the other actor. A world position when there is none |
| `minDistance`, `maxDistance` | range of the distance. A negative value keeps the starting distance |
| `springFrequency`, `springDamping` | a frequency above 0 makes it a spring |

A rope is a distance constraint with `minDistance` 0 and `maxDistance` the length. Two ends with the same value are a rigid rod.

## Cone and swing twist

A cone lets the axis lean up to `coneAngle` degrees from its rest direction.

A swing twist is made for limbs. `twistAxis` runs along the bone. `swingAngle` and `planeAngle` limit the lean in two perpendicular directions, as a round `Cone` or a square `Pyramid`. `twistMin` and `twistMax` limit the roll around the bone.

## Breaking

When the force or torque on a constraint goes over its limit, the constraint is removed and the named event `constraint.broken` is sent with the id of the actor as payload. `IsBroken()` returns true until `Repair()` is called.

## Connecting from code

```cpp
joint->ConnectTo(otherActor);
joint->ConnectToWorld();
joint->ConnectByTag();
```

`ConnectTo` and `ConnectToWorld` replace the `target` field until `ConnectByTag` goes back to it.

## Notes

* Constraints exist while the game plays. In the editor, the selected actor shows its pivot, its axis and a line to the other actor.
* Changing a field while playing rebuilds the constraint, and the rest pose becomes the current pose. Use the motor functions to drive a constraint at run time.
* A constraint is removed with either of its bodies and comes back when the body does.
* Two static bodies cannot be joined.
