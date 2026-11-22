# Mesh colliders

How to make a physics body collide with the shape of a model: rocks, buildings, terrain chunks, furniture, props.

A mesh collider is a shape of a [physics body](gravity.md), like a box or a sphere. It takes the triangles of a model and turns them into something the physics can use. Tags, profiles, sensors, events and constraints work on it as on any other shape.

## Quick start

1. Give an actor a model.
2. Add a physics body and change the type of its shape to `Mesh`.

The shape uses the model of the actor. Set `Motion Type` to static for a level, to dynamic for a prop. Nothing else is needed.

## Approximations

The same mesh can be turned into four kinds of shape. `approximation` picks one.

| Value | Shape | Use it for |
|---|---|---|
| `Auto` | triangles on a static or kinematic body, convex pieces on a dynamic one | the default |
| `Exact` | the triangles of the mesh | level geometry and anything that physics does not move |
| `Hull` | one convex shape around the mesh | simple props, fast |
| `Hulls` | several convex pieces that follow a concave mesh | a chair, a vehicle, a bowl |
| `Bounds` | the box around the mesh | a cheap stand in |

A dynamic body cannot use triangles. If `Exact` is set on a dynamic body, a hull is used and the inspector says so.

## Fields

| Field | Meaning |
|---|---|
| `mesh` | the model to use. Empty uses the model of this actor |
| `approximation` | see above |
| `submeshes` | which submeshes to use, for example `0`, `1-3` or `0,2`. Empty uses all of them. A submesh is a material slot of the model |
| `convexRadius` | rounding of the convex pieces in metres. A little helps stability |
| `maxHullVertices` | most vertices of a convex piece |
| `concavity` | `Hulls` only. How far a piece may stray from the mesh. Lower fits tighter and takes longer to build |
| `maxHulls` | `Hulls` only. Most pieces |
| `showInViewport` | draws this collider in the editor viewport when Show Physics Shapes is on. On by default, turn it off for a heavy mesh. It has no effect in the game |
| `edgeAngle` | `Exact` only. Edges flatter than this angle are smooth, so a body does not catch on the seams between triangles of a floor |

## When shapes are built

Building a shape from a model costs time, so it is done once and kept next to the model.

- The first time a body needs a shape, it is built and written to `<model>.scol`, next to the model file. The next time it is read from there.
- The file is rebuilt when the model file changes.
- Every body that uses the same model and the same settings shares one shape. A thousand rocks cost one shape. The scale of the actor is applied on top, so scaled copies share it too.
- `Hulls` can take seconds on a detailed model. It is built in the background. The body starts with a single hull and switches to the pieces when they are ready.
- A model of more than 150000 triangles is not decomposed. It keeps the single hull.

The viewport draws every mesh collider as a wireframe of the shape the body really uses, triangles for `Exact` and convex pieces for `Hull` and `Hulls`. A shape of more than 400000 triangles is drawn as its bounding box.

Replacing the model file in the editor rebuilds the shapes of the bodies that use it. The `Rebuild collider` button does it by hand.

## Baking for a release

The editor writes the `.scol` files for the shapes you used. To prepare every model of a project with the default settings, run the cooker:

```bash
ShardCooker collision example_project/example_project.json
```

It writes `Exact`, `Hull` and `Hulls` for each `.fbx`. Add `--no-hulls` to skip the slow one. Models that are already up to date are skipped. A baked project that is cooked with `ShardCooker project` carries the files.

A body whose settings are not the defaults builds its own shape the first time, and the editor writes it to the same file.

## Rays

A ray that hits a mesh collider reports the submesh it hit in `RaycastResult` (`onMesh` and `submesh`), so engine code can pick a footstep sound or an impact effect from the surface. The plugin API does not carry this yet.

## Tips

- Use `Exact` for the level and `Auto` for everything else.
- A mesh collider made of the render model is often too detailed. Point `mesh` at a simpler model, or use `submeshes` to leave out small parts.
- A sensor works best with a convex shape. Use `Hull` or `Bounds` for a trigger volume.

## Limits

- `Exact` needs a body that physics does not move.
- Two triangle meshes do not collide with each other. One of the two needs a convex shape.
- `Hulls` expects closed surfaces. When the decomposition fails, a single hull is used.
- There is no physical material per submesh yet. All of a shape has one profile.
