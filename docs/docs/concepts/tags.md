# Tags

How to label worlds, actors and components with names, and find or filter them by those names, without adding a component or a script for it.

A tag is a dotted, hierarchical name : `Enemy.Undead.Skeleton`, `Damage.Fire`, `Interaction.Door.Locked`. Each dot goes one level down a tree, so a tag is also *every one of its parents* : something tagged `Enemy.Undead.Skeleton` answers yes to "is it an `Enemy` ?" and to "is it an `Enemy.Undead` ?".

Everything lives in `engine/core/tags/` : `Core::Tag` (one name), `Core::TagContainer` (a set of them), `Core::TagQuery` (a condition over a set) and `Core::TagRegistry` (the tree of every tag the project knows), reached with `Core::TagRegistry::Get()`.

## The tree

Tags are declared once, for the whole project, and form a tree :

```
Enemy
├── Enemy.Undead
│   ├── Enemy.Undead.Skeleton
│   └── Enemy.Undead.Ghost
└── Enemy.Boss
Damage
├── Damage.Fire
└── Damage.Ice
Interaction
└── Interaction.Door
    └── Interaction.Door.Locked
```

Declaring `Enemy.Undead.Skeleton` declares `Enemy.Undead` and `Enemy` with it. A tag can come from three places, merged into the same tree when the project opens :

| Source | Where | Typically |
|---|---|---|
| Project | `"tags"` in the project file, edited in *Project settings > Tags* | the game's own tags |
| Plugin | `"tags"` in the plugin's manifest | the tags a plugin's components look for |
| Code | `SH_DECLARE_TAG(variable, "Name")` in a `.cpp` | tags a C++ system depends on, so it cannot be deleted from the editor |

```json
"tags": {
    "Enemy.Undead.Skeleton": { "comment": "Breaks into bones, immune to poison" },
    "Enemy.Boss": {},
    "Damage.Fire": {},
    "Interaction.Door.Locked": {}
}
```

A name is made of segments of `A-Z a-z 0-9 _` separated by single dots, and is case sensitive (`Enemy` and `enemy` are two tags). A tree is at most 8 levels deep.

The tree is built again when a source changes : when the project is loaded or its tags are edited, and when a plugin is loaded or unloaded. `TagRegistry::Get().GetRoots()`, `GetInfo(tag)` (name, parent, children, who declares it, comment) and `CollectDescendants(tag, out)` walk it.

## One tag

`Core::Tag` is a `StringId` (see `engine/core/containers/string_id.hpp`) that the registry knows : copying and comparing it costs one integer, and the text is kept for logs and the editor.

```cpp
using namespace Shard::Engine::Core;

Tag skeleton = Tag::Request("Enemy.Undead.Skeleton");   // looked up in the registry
Tag parent   = skeleton.GetParent();                      // Enemy.Undead
bool under   = skeleton.IsChildOf(Tag::Request("Enemy")); // true
std::string text = skeleton.ToString();                   // "Enemy.Undead.Skeleton"
```

`Tag::Request` of a name the registry does not know returns the empty tag (`IsValid()` false) and logs a warning once per name : a typo does not silently create a tag nobody else uses. What a file holds is read with `TagRegistry::Get().Resolve(name)` instead, which keeps an unknown name (see [Renaming and deleting](#renaming-and-deleting)).

For tags a C++ system tests every frame, declare them once and use the variable :

```cpp
// door_system.cpp
SH_DECLARE_TAG(TagDoorLocked, "Interaction.Door.Locked");

if (actor->GetTags().HasTag(TagDoorLocked)) ...
```

The variable is filled before any world loads, so it can be used from `OnCreate` on.

## Who carries tags

| Object | Get the tags | Saved in |
|---|---|---|
| `Actor` | `actor->GetTags()` | the world file, `"tags"` of the actor |
| `Component` | `component->GetTags()` | the world file, `"tags"` of the component (a reserved key, like `"type"` and `"active"`) |
| `World` | `world->GetTags()` | the world file, `"tags"` at its root |

Each of them owns a `TagContainer`. Tags on a component let an actor say different things about its parts : the `PhysicsBody` of a door's handle tagged `Interaction.Door`, its frame not. Only these three carry tags : assets do not.

```json
{
    "name": "Skeleton_03",
    "tags": ["Enemy.Undead.Skeleton", "Faction.Necromancer"],
    "components": [
        { "type": "physics_body", "tags": ["Hitbox.Head"], ... }
    ]
}
```

Tags are not inherited down the actor hierarchy : the children of an actor tagged `Enemy` are not enemies. `Clone()` copies the tags of the actor and of its components.

## Testing a set

A `TagContainer` holds the tags it was given (its *explicit* tags) and answers questions with their parents included :

| Call | True when the container has... | `{Enemy.Undead.Skeleton}` with `Enemy` |
|---|---|---|
| `HasTag(t)` | `t`, or a child of `t` | true |
| `HasTagExact(t)` | `t` itself | false |
| `HasAny(other)` | at least one tag of `other` (with `HasTag`) | |
| `HasAll(other)` | every tag of `other` (with `HasTag`) | |
| `HasAnyExact` / `HasAllExact` | the same, with `HasTagExact` | |

The matching only goes one way : a container holding `Enemy` does not have `Enemy.Undead.Skeleton`. Tag things with the most precise name, and test with the most general one that the logic needs.

```cpp
TagContainer& tags = actor->GetTags();

tags.Add(Tag::Request("Status.Burning"));
tags.Remove(Tag::Request("Status.Frozen"));

if (tags.HasTag(Tag::Request("Enemy")) && !tags.HasTag(Tag::Request("Enemy.Boss")))
    ApplyKnockback(actor);
```

`Add` of a tag the container already has does nothing ; `Remove` only removes an explicit tag (removing `Enemy` from `{Enemy.Undead}` does nothing, remove `Enemy.Undead`). `RemoveWithChildren(t)` removes `t` and every explicit tag under it.

## Queries

When a condition is more than one test, write it as a `TagQuery` : it can be built once, stored in a component field, edited in the editor, and evaluated against any container.

```cpp
TagQuery hostile = TagQuery::All({"Enemy"})
                       .And(TagQuery::None({"Enemy.Boss", "Status.Dead"}));

if (hostile.Matches(actor->GetTags())) ...
```

The same query as text, the form it is saved and typed in :

```
Enemy & !(Enemy.Boss | Status.Dead)
```

`&` is *and*, `|` is *or* (looser than `&`), `!` is *not*, parentheses group, and a bare name means `HasTag`. Write `=Enemy` for `HasTagExact`, and `()` for "nothing". `TagQuery::Parse(text)` returns an empty query (that matches everything) and logs the error when the text is not valid ; `TagQuery::TryParse(text, query, &error)` reports it instead. `ToString()` gives the text back.

A name the registry does not know, in the text or in `All`/`Any`/`None`, is kept and reported : that term matches nothing, it does not disappear and widen the query.

A reflected field of type `TagQuery` (or `Tag`, or `TagContainer`) is saved by name and has its own widget in the editor's inspector, so a designer can say which actors a trigger reacts to without code (run `scripts/regen_reflection.sh` after adding one, like any reflected field) :

```cpp
class DamageZone : public Script {
    FIELD(Editable, tooltip=Which actors the zone hurts)
    TagQuery affects = TagQuery::Parse("Enemy | Player");

    FIELD(Editable)
    Tag damageType;
    ...
};
```

## Finding tagged things in a world

Each world keeps an index from tag to actors (and to components), updated when a tag is added or removed and when an actor joins or leaves the world, so a lookup does not walk every actor :

```cpp
auto* world = actor->world;

std::vector<std::shared_ptr<Actor>> skeletons = world->FindActorsWithTag(Tag::Request("Enemy.Undead"));
std::shared_ptr<Actor> player = world->FindFirstActorWithTag(Tag::Request("Player"));
std::vector<std::shared_ptr<Actor>> targets = world->FindActors(TagQuery::Parse("Enemy & !Status.Dead"));
```

| Call | Cost |
|---|---|
| `FindActorsWithTag(t)` | the number of actors under `t` |
| `FindFirstActorWithTag(t)` | the same : the first of them |
| `FindActors(query)` | the actors under the query's required tag (a bare name, or a bare name in its top-level `&`), then the query tested on each. A query without one (`A \| B`, `!A`) tests every actor of the world |
| `FindComponentsWithTag(t)` / `FindComponentsWithTag<T>(t)` | the number of components under `t` |

`QuerySphere` and `QueryAABB` add a position to it, so "the enemies within 10 m" is one call instead of a query and a loop :

```cpp
std::vector<std::shared_ptr<Actor>> results;
world->QuerySphere(center, 10.0, TagQuery::Parse("Enemy"), results);
world->QueryAABB(Core::AABB(min, max), TagQuery::Parse("Pickup"), results);
```

They test the position of the actors `FindActors` finds (`Transform::GetWorldCoords`, in double) : the tags narrow the search first, they are not a spatial index.

The results are in the order of the actors' ids, so a deterministic run (see [Headless & determinism](headless.md)) finds them in the same order every time. Deactivated actors (and components) are left out ; pass `includeInactive = true` to keep them.

## Reacting to changes

Changing tags in a loaded world emits a `TagsChangedEvent` to the scope of what changed, see [Event system](events.md) :

| Tags of | Scope | Subscribe with |
|---|---|---|
| an actor | the actor | `subscribeToActor<TagsChangedEvent>(actor->GetID(), ...)` |
| a component | the component, by its id in the world | `subscribeToComponent<TagsChangedEvent>(actor->GetComponentIDInWorld(component->GetLocalId()), ...)` |
| a world | the world, by its asset id | `subscribeToWorld<TagsChangedEvent>(world->ResolveAssetID(), ...)` |

```cpp
events->subscribeToActor<Events::TagsChangedEvent>(actorID, [](const Events::TagsChangedEvent& e) {
    if (e.added.HasTag(TagBurning))
        StartFireParticles();
});
```

`added` and `removed` are the explicit tags that changed, in one event per `Add`, `Remove`, `RemoveWithChildren`, `Clear` or assignment (`actor->GetTags() = other`). Loading a world does not emit them. An actor's tags are read before its components, so a script's `OnCreate` already sees them ; a component's own tags come after it is created : read those in `OnWorldLoaded` or `OnPlay`.

## Renaming and deleting

Renaming a tag in *Project settings > Tags* renames it and all of its children, and adds a redirect per renamed name so worlds saved with the old name still load :

```json
"tagRedirects": { "Enemy.Undead.Skelly": "Enemy.Undead.Skeleton" }
```

The open world is renamed at once (its actors, components, reflected tag fields and its own tags : `Worlds::RemapTags`) and marked as changed ; the other worlds take the new names through the redirects when they are loaded, and keep them once saved. The redirects are listed under the tree, where one can be removed.

The magnifier of a tag lists what uses it (or a tag under it) in the worlds of the project, and so does deleting one, before it is confirmed. The open world is searched as it is (exactly, field types included) ; the other worlds are read from their files, where any string of a component that reads as a query naming the tag is listed, even when it is not a tag field. A tag declared from code or by a plugin (or with such a tag under it) can be neither renamed nor deleted from the editor.

Loading a tag the project does not know (the plugin that declared it is not loaded, the redirect was removed, the tag was deleted) keeps it as it was read and writes it back on save, like unknown components, and logs a warning once. It matches only itself, and the editor shows it in orange.

## In the editor

* **Properties** : the actor's tags under its name, and a `tags` row at the top of each component. Click a tag to remove it, `+` to pick one in the tree (with a search box). Both edits can be undone.
* **World Settings > General** : the world's own tags.
* **Reflected fields** : a `Tag` is a button that opens the tree, a `TagContainer` the same chips as above, and a `TagQuery` a text field checked as it is typed (a red frame and the reason in its tooltip while it is not valid). It is applied when the field is left ; its `+` adds a tag as one more `&` term.
* **Project settings > Tags** : the tree, who declares each tag, the comments (editable for the project's own), and add, rename, delete and find references. Saved with the project file, like the other settings.

## Rules to know

* **Tags are labels, not state.** A tag says what something *is* or *is in* (`Enemy`, `Status.Burning`). A value that changes every frame or carries a number belongs in a component field.
* **Not thread-safe to change.** Add and remove tags from the main thread, outside parallel systems. Reading a container from a parallel system is fine as long as nothing changes it during the phase.
* **Tags are not collision layers.** What collides with what stays in the physics settings ; a tag on a body only says what it is when a script reads it in `OnContactAdded`.
* **Keep the tree shallow and general.** `Enemy.Undead.Skeleton` and `Faction.Necromancer` as two tags rather than `Enemy.Undead.Skeleton.Necromancer` : a tag answers one question.
* **Declare tags where they belong** : the project, a plugin's manifest, or `SH_DECLARE_TAG`. The tree changes on the main thread only (a project edit, a plugin loaded or unloaded), never from a parallel system.

See also : [Event system](events.md), [Plugins](plugins.md) for declaring tags in a manifest.
