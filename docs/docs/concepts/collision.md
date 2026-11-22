# Collision

How to decide which actors collide, how to react when they do, and how to find out why two actors do not.

A collider has a profile. The profile says what the collider is and how it reacts to everything else, using [tags](tags.md). Scripts, the editor and queries all read the same rules.

## Quick start

1. Add a collider to an actor and pick a profile in its inspector, for example `Skeleton`.
2. To react to contacts, add a callback to a script on the actor.

```cpp
void OnOverlapBegin(const Shard::OverlapEvent& e)
{
    if (e.other.HasTag("Player")) Open();
}
```

3. If nothing happens, select both actors and open the Explain tab of the Collision panel. It says which rule decided.

## Tags and identity

A collider has an identity: the tags that say what it is, such as `Enemy.Undead.Skeleton` or `World.Terrain`. Mark the tags used for collision in the project tags with `"collision": true`. The mark applies to every tag below it.

```json
"tags": {
    "Enemy": { "collision": true },
    "World": { "collision": true },
    "Projectile": { "collision": true }
}
```

Other tags, such as a status or a gameplay state, are not part of the identity. A rule can still test them, see [Rules on other tags](#rules-on-other-tags).

## Profiles

A profile has a name and is made of the fields below.

| Field | Meaning |
|---|---|
| `base` | profile to inherit from |
| `identity` | collision tags of the collider |
| `rules` | ordered list of `{ target, response }` |
| `default` | response when no rule matches |
| `mode` | `Solid`, `Sensor`, `QueryOnly` or `Disabled` |
| `material` | physics material |
| `events` | hit threshold and overlap options |

Profiles are declared in the project file, in a plugin manifest, or in code.

```json
"collision_profiles": {
    "Skeleton": {
        "base": "Character",
        "identity": ["Enemy.Undead.Skeleton"],
        "rules": [
            { "target": "Player.Projectile", "response": "Overlap" },
            { "target": "Enemy",             "response": "Ignore" },
            { "target": "World | Prop",      "response": "Block" }
        ],
        "default": "Ignore"
    }
}
```

A target is a [tag query](tags.md). `Enemy` matches every enemy, including ones added later. The first rule that matches is the one that applies.

An actor can override its mode, its default response and what its queries ignore in its inspector.

## Responses

| Response | Effect |
|---|---|
| `Ignore` | the two colliders do not interact |
| `Overlap` | contact is reported, nothing is pushed |
| `Block` | contact is reported and the colliders push each other |

Each collider answers for itself. The pair gets the more cautious of the two answers, in the order `Ignore`, `Overlap`, `Block`. A collider that blocks another collider that only overlaps it gets an overlap.

## Modes

| Mode | Behavior |
|---|---|
| `Solid` | takes part in the simulation |
| `Sensor` | detects other colliders and never pushes them |
| `QueryOnly` | ignored by the simulation, found by queries |
| `Disabled` | does nothing |

A static sensor detects moving colliders that are awake. To detect static colliders too, make the sensor kinematic. Two sensors never detect each other.

## Physics materials

A physics material is a `.physmat` file in the project, like a rendering material. Create one in the asset browser with Create, Physics Material, and double click it to open its editor. A profile refers to it by its path in the project, for example `physics/Steel.physmat`.

```json
{
    "friction": 0.8,
    "restitution": 0.1,
    "density": 7800.0,
    "combine": "Max",
    "surface": "Surface.Metal.Steel"
}
```

| Field | Meaning |
|---|---|
| `friction` | friction coefficient |
| `restitution` | bounciness, 0 to 1 |
| `density` | kg per cubic meter, used for the mass of a body when its shape has no explicit mass |
| `combine` | how the value is merged with the other body's material: `Average`, `Min`, `Max` or `Multiply` |
| `surface` | surface tag, which sounds, decals and damage rules use |

Saving a `.physmat` file, in the editor or on disk, applies it to the running world. Renaming or moving a `.physmat` file, or a folder that contains some, updates the profiles that use it. Deleting one leaves the profiles unchanged, and they then use no material. A plugin can still declare named materials with `physics_materials` in its manifest.

## Finding out why

```cpp
auto why = Shard::Physics::Explain(actorA, actorB);
```

`why` gives, in order, the mode and motion type of each actor, the rule that matched on each side with its profile and position in the list, any active override and who set it, and the final response. The Collision panel shows the same text when two actors are selected, and a click on a cell of the project matrix shows it for two profiles.

### Warnings

The editor lists problems it finds in the rules.

| Warning | Meaning |
|---|---|
| Dead rule | its target matches no identity in the project |
| Shadowed rule | an earlier rule always matches first |
| Isolated profile | collides with nothing |
| One sided pair | one profile blocks, the other ignores |
| Impossible listener | a script waits for overlaps between two profiles that never overlap |

A plugin that subscribes at run time gets the last warning in the log when it subscribes.

## Events

Scripts receive `OnOverlapBegin`, `OnOverlapEnd` and `OnHit`. A coroutine can wait for one with `WaitForEvent`.

An event exists for a pair as soon as something listens for it, and costs nothing when nobody does. A script written with the SDK names the kinds it wants.

```cpp
class Door : public Shard::Script {
public:
    static constexpr uint32_t kCollisionEvents = Shard::CollisionKinds::OverlapBegin | Shard::CollisionKinds::OverlapEnd;
    void OnOverlapBegin(const Shard::CollisionEvent& e) override { }
};
```

A script of the engine calls `ListenCollisions()` once, usually in `OnCreate`, and stops with `StopListeningCollisions()`.

A listener can filter on the other collider's tags, ask for a minimum impact speed, or ask to fire once.

### Overlaps

An actor with several colliders produces one Begin when it first touches something and one End when it stops. `Overlapping()` returns what a collider touches at that moment.

```cpp
for (auto other : trigger.Overlapping()) { }
```

### Begin and End

Every Begin is followed by an End. When a collider is destroyed, disabled, moved directly, or its profile changes so that the pair is ignored, the End is sent with a cause.

| Cause | Meaning |
|---|---|
| `Separated` | the shapes stopped touching |
| `Destroyed` | an actor or collider was destroyed |
| `Disabled` | the body or its mode was disabled |
| `ProfileChanged` | the pair is now ignored |
| `Teleported` | the position was set directly |

### Event data

| Field | Meaning |
|---|---|
| `kind` | `OverlapBegin`, `OverlapEnd` or `Hit` |
| `step` | fixed step number |
| `self`, `other` | the actors |
| `selfCollider`, `otherCollider` | the colliders |
| `point` | position in the world, in 64-bit coordinates |
| `normal` | direction pointing out of `other` |
| `impactSpeed` | speed of approach along the normal, before the collision is resolved |
| `impulse` | estimated from the impact speed and the masses |
| `surface` | surface tag of `other`, as text |
| `cause` | for `OverlapEnd` |

Events are delivered after each fixed step, always in the same order for the same simulation.

## Temporary changes

A script can change a collider for a while and give the change back automatically.

```cpp
auto scope = actor.CollisionOverride({ .ignore = "Enemy" });
co_await Wait(0.3f);
```

The change ends when `scope` is destroyed or when the actor is. Several changes on one collider apply by priority.

`IgnoreCollisionWith(other)` skips one pair, for example a projectile and the actor that fired it. An actor never collides with its own parent or children.

### Rules on other tags

A rule target can use tags that are not collision tags, for example `Status.Frozen`. These rules are checked when a contact starts instead of once for the whole project, so they cost a little more. Use collision tags when you can.

## Queries

A query can run as a profile or as an actor, and then follows the same rules as that collider.

```cpp
Shard::QueryFilter filter;
filter.asActor = actor;
filter.hitOverlaps = true;
auto hits = Shard::Physics::RaycastAll(from, to, filter);
```

`asProfile` takes a profile name, `asActor` an actor and wins when both are set. By default a query meets what the collider would block. With `hitOverlaps` it meets what the collider would overlap too. The tag filter still applies on top. Queries that use the layer masks `Static` and `Moving` keep working.

## Interaction table

A list in the project file of what happens when two kinds of things meet, so that common reactions need no script. It is stored under `collision_interactions` and edited in Project settings, Collision.

| A | B | Event | Min speed | Actions |
|---|---|---|---|---|
| `Projectile.Fire` | `Enemy.Undead` | Hit | 0 | `AddTag(Status.Burning)`, `Destroy(self)` |
| `Impact.Heavy` | `Surface.Metal` | Hit | 3 | `SendEvent(clang)` |

When several rows match, the most specific one is used. If a surface has no row, the table looks one level up the tag tree: `Surface.Metal.Steel`, then `Surface.Metal`, then the default row.

Built in actions are `Destroy`, `ApplyImpulse`, `SendEvent`, `AddTag` and `RemoveTag`. A plugin or a script can register more, for damage, sounds or effects.

## Recording

The engine keeps the last few seconds of contacts, events, queries and temporary changes. It is on in the editor and off in a built game. The Recorder tab of the Collision panel steps through the steps one by one, so a contact that happened too fast to see can still be inspected.

## Editor

| Tool | Use |
|---|---|
| Project settings, Collision | collision tags, profiles, the interaction table, the matrix of every profile against every other (read only, click a cell to see why), and the list of warnings with a jump to the profile |
| Physics material editor | friction, restitution, density, combine mode, surface tag and the profiles that use the material (opens from a `.physmat` file) |
| Collider inspector | profile, overrides of mode, default response and ignored queries, and a Highlight collisions button |
| Collision panel, Explain | the result between two selected actors (Block, Overlap or Ignore) with what it means, then for each actor its profile, mode, moving or static, material, overrides and the rule that decided its side. The swap button inverts first and second, and the full trace can be copied |
| Collision panel, Recorder | step by step replay of the recording |
| Collision panel, Live | collision classes in use, subscriptions, what the selected actor overlaps, and the viewport overlay settings |
| Viewport | collider wireframes are colored by mode: cyan solid, green sensor, yellow query only, grey disabled |
| Highlight collisions | colors every collider by what the selected one does with it: red blocks, blue overlaps, dark ignores, pink is the selected one |
| Viewport overlay | contact points, normals and an event log that fade out, turned on in the Live tab |

## From a plugin

The `shard.collision` interface gives plugins the same features.

| Function | Use |
|---|---|
| `subscribe`, `unsubscribe` | listen to events, with a filter |
| `poll_events` | every event of the last step, read once per step |
| `get_overlaps` | what a collider touches now |
| `explain` | the explanation for two actors |
| `push_override`, `pop_override` | a temporary change |
| `as_profile`, `as_actor`, `hit` | fields of a query filter that make a cast, sweep or overlap follow a collider's rules |

A plugin's subscriptions end when the plugin is unloaded. A collider built by a plugin takes a profile in its description, and uses `Default.Static` when none is given.

## Default profiles

Actors with no profile use `Default.Static` or `Default.Moving`, depending on their motion type. Static and moving colliders collide, moving colliders collide with each other, and static colliders ignore each other.
