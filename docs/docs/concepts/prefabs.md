# Prefabs

A prefab is an actor, with its components and its children, kept in a file of the project (`prefabs/crate.prefab`) so that it can be built again and again : in a world, or from a script.

Its file is the JSON of an actor, the one a world file holds. The engine finds a prefab by its path in the project (`prefabs/crate.prefab`) or by its bare name (`crate`). The content of a module (`prefabs/<name>.json`) is looked at when the project has none.

## In the editor

- **Create Prefab** (outliner, right click on an actor) writes `prefabs/<name>.prefab` with the actor and its children, its root at the origin, and turns the actor into an instance of it.
- Drop a `.prefab` from the asset browser on the viewport (placed where the mouse points) or on the outliner (placed in the folder it is dropped on). Each drop can be undone.
- On an instance : **Select Prefab Root** from any of its actors ; on its root, **Apply To Prefab** writes the instance back into the file, and **Unpack Prefab** cuts the link and keeps the actors as they are.

## Instances

Spawning a prefab builds an **instance** : one unit, a root actor and its children. The root remembers which prefab it comes from. From any actor of the instance :

| C++ | |
|---|---|
| `actor.IsPrefabInstance()` | the actor is the root of an instance, or part of one |
| `actor.PrefabName()` | the path of its prefab |
| `actor.PrefabRoot()` | the root of the instance |
| `actor.DestroyPrefab()` | destroys the whole instance |

Destroying a child on its own is allowed : the instance goes on with what is left.

## From a script

```cpp
Shard::Scene scene = GetScene();

Shard::Actor crate = scene.SpawnPrefab("crate");

Shard::Pose pose;
pose.position = { 4.f, 0.f, 2.f };
Shard::PrefabOptions options;
options.pose = &pose;
options.name = "crate_a";
options.overridesJson = R"({"components":{"model":{"staticLighting":false}}})";
Shard::Actor placed = scene.SpawnPrefab("crate", options);

std::vector<Shard::Actor> crates = scene.FindPrefabs("crate");
scene.DestroyPrefabs("crate");
```

The pose of `PrefabOptions` is applied **before the children are built** : a light or a physics body in the prefab reads the world transform of its parent when it is created, so moving the root afterwards would leave them where the prefab was.

Finding goes by the name of the prefab (`"crate"`) or by its path (`"prefabs/props/crate.prefab"`) to tell two prefabs of the same name apart. Roots come back in the order of their ids, the same on every machine, and the deactivated ones are left out. The ABI is `shard.worlds` 1.2 (`shard_worlds.h`).

## In a world file

A world does not copy a prefab : it keeps a **link** to it and what differs.

```json
{ "name": "crate_a", "prefab": "prefabs/crate.prefab",
  "overrides": {
    "components": { "transform": { "position": { "x": 4.0 } } },
    "children": { "lid": { "components": { "transform": { "position": { "y": 1.4 } } } } },
    "addChildren": [ { "name": "label", "components": [ { "type": "transform" } ] } ],
    "removeChildren": [ "hinge" ]
  } }
```

- A component is matched by its `type` (`light`, then `light#1` for a second one), a child by its `name` (`lid`, then `lid#1`).
- The fields of a component are a merge patch : the fields that are written replace the prefab's, `null` removes one, an array is replaced whole.
- `addComponents`, `removeComponents`, `addChildren`, `removeChildren` add and remove. `folder` and `tags` are overridable too.
- Saving a world writes this form again from what the instance is now, so an edit of the prefab reaches the instances at the next load.
- A prefab that is not found is kept as it is in the file, and written back unchanged.
- A prefab can contain instances of other prefabs.

Hot reloading a prefab does not rebuild the instances already in a loaded world yet.
