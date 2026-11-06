# Write a game with the SDK

This tutorial builds a game as a plugin, using only the Shard SDK : a folder of headers and a CMake package. You do not need the engine's source or its libraries, and any C++17 compiler works (GCC, Clang, MSVC), whichever one built the engine. The game ends up as several source files : scripts that live on actors, a component with a system, and a console variable.

## What the SDK is, in a few lines

Your game is compiled apart from the engine, so the two cannot share C++ objects. They talk through a small set of plain C functions, the **C ABI**. The **C++ SDK** is a layer of headers that hides those C functions behind classes and lambdas (`Shard::Script`, `s.Each(...)`), so you write ordinary C++ and never call the C functions yourself. It is compiled into your plugin and costs nothing at run time when the code stays simple. You only need to know C++ syntax (classes, templates used as shown, lambdas) to follow this tutorial. The full picture, and what can make a plugin slower, is in [The C ABI and the SDK](../concepts/plugins.md#the-c-abi-and-the-sdk).

## 1. Get the SDK

From an engine build :

```sh
cmake --install build --component sdk --prefix <dir>
```

The SDK is installed in `<dir>/sdk` : `include/` (the headers), `cmake/` (the CMake package), `bin/ShardPkg` (packs and signs plugins), `templates/plugin/` and `VERSION`. The `ShardSDKArchive` target puts the same folder in `build/shard-sdk-<version>.zip`, and the CI publishes one per OS (artifacts `shard-sdk-Windows` and `shard-sdk-Linux`). In the engine's repository, the `sdk/` folder is already an SDK, without `bin/`.

## 2. Start from the template

Copy `sdk/templates/plugin` to a folder of your own :

```
my-game/
  CMakeLists.txt        find_package(ShardSDK 1.0) and shard_sdk_add_plugin : rename the target (template_plugin) and the project
  shard-plugin.json     the manifest : change "id" (reverse DNS, lowercase, like com.yourname.mygame)
  src/
    module.cpp          the plugin's entry point : console variables, content, hot reload
    module.hpp          what the files share with it
    gameplay/spinner.*  a script, in a header and a source file
    ecs/drift.cpp       a component and a system
  content/              files the plugin reads through mod://<id>/
```

Every `.cpp` under `src/` is part of the plugin (`SOURCE_DIRS src` in the CMakeLists), in whatever folders you put them. To add a feature you add files : the CMakeLists does not list them.

## 3. Build it

```sh
cmake -S my-game -B my-game/build -DCMAKE_PREFIX_PATH=<dir>/sdk
cmake --build my-game/build
```

The plugin is written to `my-game/build/plugins/<id>/` : the manifest (the SDK adds `"abi": "c1"` and the path of the library), `bin/<platform>/` and `content/`. The library exports a single symbol, `shard_plugin_entry`. Built with MinGW it carries its own C++ runtime. Built with MSVC it needs the Visual C++ runtime, like any MSVC program.

## 4. Load it

Pass the folder to the engine :

```sh
ShardHeadless --game <game library> --project <project.json> --plugin my-game/build/plugins/<id> --list-plugins
```

You can also copy it into the project's plugins folder, where the editor and the player find it. `--check-plugins` runs the simulation twice, once with one job thread and once with several, and fails if the final state differs. A plugin that affects the simulation has to pass it.

If the engine says `built for the C ABI 1.4, this engine has 1.3`, it is older than the SDK. Build against an SDK that is not newer than the oldest engine you want to support (see step 9).

In the editor, the script shows up in the *Add component* menu of the inspector as `<id>.Spinner`. Add it to an actor and press play.

## 5. Write a script

A script is a class that lives on an actor. The engine creates it with the component and calls it the way it calls its own scripts. Here is `Health`, in two files :

```cpp
// src/gameplay/health.hpp
#pragma once
#include <shard/game.hpp>

class Health : public Shard::Script {
public:
    // Fields : shown in the inspector, saved with the world. Each one is declared where it is, with its options
    SHARD_FIELD(float, maxHealth, Range(1.f, 1000.f), Tooltip("Hit points at the start")) = 100.f;
    SHARD_FIELD(float, current, Runtime()) = 100.f;     // Runtime : shown, not saved
    SHARD_FIELD(Shard::Text, label);                    // a text, edited in the inspector
    float invulnerableFor = 0.f;                        // not a field : a normal member

    void OnPlay() override { current = maxHealth; }
    void OnTick(float deltaTime) override;

    void Damage(float amount) { current -= amount; }
};
```

```cpp
// src/gameplay/health.cpp
#include "health.hpp"

void Health::OnTick(float)
{
    if (current <= 0.f) Owner().Destroy();  // destroys the actor, its components and its children. This script gets OnDestroyed
}

SHARD_REGISTER_SCRIPT(Health)
```

The last line registers the class. Nothing else refers to the file. In the editor the script is the component `<id>.Health`.

`SHARD_FIELD(Type, name, options...)` declares the member (the default value goes after it, like for any member) and tells the SDK its name, its type, where it is in the object and its options. The options are `Range(min, max)`, `Tooltip("text")`, `Runtime()` (shown and read only, not saved) and `Hidden()` (neither shown nor saved). Give every field a default : a plain `float x;` starts at zero while the class has no constructor of its own, and with whatever was in memory otherwise.

A script can override these :

| Callback | Called |
|---|---|
| `OnCreate` | the script joined an actor that is in a world |
| `OnPlay`, `OnStop` | play mode starts, stops |
| `OnTick(dt)` | every frame |
| `OnFixedTick(dt)` | every physics step, before the step. Use it for forces and anything that must be reproducible |
| `OnLateTick(dt)` | every frame, after everything has moved. Use it for cameras |
| `OnActivated`, `OnDeactivated`, `OnDestroyed` | the script or its actor changes state or is destroyed |
| `OnWorldLoaded`, `OnWorldUnloaded` | the world finished loading, is about to unload |

Members that are not declared with `SHARD_FIELD` stay out of the inspector and the world file. In a copy of the actor made for play mode they start fresh.

The older way still works : `SHARD_REGISTER_SCRIPT(Health, (maxHealth)(current))` lists the members in the `.cpp` (`SHARD_REGISTER_SCRIPT_EX` adds options), and the two can be mixed in one class.

## 6. Talk to the world

A script gets its actor with `Owner()` and reaches the rest of the world from there :

```cpp
// src/gameplay/player.cpp
#include "player.hpp"
#include "health.hpp"                       // a script from another file : include its header, nothing to link

void Player::OnTick(float dt)
{
    const float turn = Shard::Input::Axis(Shard::Key::A, Shard::Key::D);
    const float forward = Shard::Input::Axis(Shard::Key::S, Shard::Key::W);
    Owner().Translate(Owner().Forward() * (forward * speed * dt));
    Owner().SetRotation(Shard::AxisAngle({ 0.f, 1.f, 0.f }, -turn * 2.f * dt) * Owner().Rotation());

    Shard::Scene scene = GetScene();                        // the world this actor is in
    if (Shard::Input::WasPressed(Shard::Key::Space)) {
        Shard::Actor bullet = scene.Create("Bullet");
        bullet.SetWorldPose(Owner().WorldPose());
        bullet.Add<Shard::Light>().SetIntensity(3.f);                     // an engine component, as a typed handle
        bullet.AddScript<Bullet>();                                       // a script of this plugin
    }
    if (Shard::Actor enemy = scene.Find("Boss"))
        if (Health* health = enemy.GetScript<Health>())
            health->Damage(10.f);
}

SHARD_REGISTER_SCRIPT(Player)
```

* `Shard::Actor` is a handle. Copy it freely and keep it in a member. Once the actor is destroyed, `IsValid()` returns false, getters return zeroes and setters do nothing.
* Pose : `Position`, `Rotation` and `Scale` are relative to the parent. `WorldPosition`, `WorldRotation` and `WorldPose` are in world space. There are also `Forward`, `Right`, `Up`, `LookAt` and `Translate`.
* Hierarchy and state : `Parent`, `SetParent`, `Children`, `Name`, `IsActive`, `SetActive`, `Destroy`, `AddTag`, `HasTag`.
* `GetScene()` gives a `Shard::Scene` : `Create`, `Spawn` (an actor from the JSON a world file holds, such as a prefab), `Find` by name and `FindWithTag`.
* `Shard::Input` reads the keyboard, mouse and gamepads. It reads what replays record, so a recorded run plays back the same.
* The maths : `Shard::Vec3` and `Shard::Quat` with the usual operators, plus `Dot`, `Cross`, `Normalize`, `Lerp`, `AxisAngle` and `LookRotation`.

## 7. Systems

A script per bullet is fine for a hundred bullets. For thousands, keep the data in a component and write a system that works on all of them at once. Systems that do not conflict run in parallel. A system lives in its own file and registers itself like a script does :

```cpp
// src/ecs/projectiles.cpp
#include <shard/game.hpp>

struct Projectile { Shard::Vec3 velocity; float life = 3.f; };
SHARD_DEFINE_COMPONENT(Projectile, (velocity)(life))        // found anywhere with Shard::ComponentOf<Projectile>()

SHARD_ON_LOAD(ProjectileSystem, ctx)                  // runs in OnLoad, after every component is registered
{
    const auto projectile = Shard::ComponentOf<Projectile>();
    const auto pose = ctx.Components().Find<Shard::Pose>("shard.LocalTransform");
    ctx.Systems().Add("fly", Shard::Phase::Update).Writes(projectile, pose).Run([projectile, pose](Shard::SystemCtx& s) {
        s.Each(projectile, [&](Shard::Entity e, Projectile& b) {
            b.life -= s.DeltaTime();
            if (Shard::Pose* p = s.Get(e, pose)) { Shard::Pose moved = *p; moved.position += b.velocity * s.DeltaTime(); s.SetLocalTransform(e, moved); }
            if (b.life <= 0.f) s.Destroy(e);
        });
    });
}
```

Keep the row loop small. The function you give to `Each` runs once per entity, so with 100 000 entities it runs 100 000 times per frame. Put only the arithmetic on the component in it. Read what does not change (`s.DeltaTime()`) once before the loop, and call something heavier (`s.Emit(...)`, `s.Destroy(e)`, a log line) only inside an `if`, when it really happens. Avoid creating a `std::string` or a `std::vector` in it.

This is the only real cost of writing a system with the SDK. The SDK itself adds nothing to a clean loop : measured on 100 000 entities, the same system written as a plugin takes the same time as inside the engine, and about 10 % more with an `Emit` that fires now and then. An `Emit` written in a loop without care cost 1.5 times the engine before the SDK kept it out of line. The C ABI underneath costs one call per chunk of a few hundred entities, which does not show.

A system declares every component type it touches. It cannot use actors, scripts or input, which are only available in script callbacks (scripts run one after another, systems run side by side).

The registrations of all files run at the start of `OnLoad`, in a fixed order : components, then scripts, then systems, then the `SHARD_ON_LOAD` blocks sorted by name. The order does not depend on how the compiler linked your files, which matters because it decides component ids and system order. `SHARD_GAME_MODULE()` makes a plugin that is only these registrations. The template instead has a class in `module.cpp` for what is not gameplay : variables, commands, content and hot reload.

## 8. Physics, sound, worlds and the editor

A script reaches these through `Shard::Actor` and a few static classes. All of them work from script callbacks.

```cpp
class Turret : public Shard::Script {
public:
    Shard::Actor target;            // an actor of the world, picked by name in the inspector
    std::string note;               // a std::string works as a field

    void OnFixedTick(float) override
    {
        if (!target) return;
        // Look at the target and fire a ray at it. The turret itself is ignored.
        auto hit = Shard::Physics::Raycast(Owner().WorldPosition(), target.WorldPosition() - Owner().WorldPosition(), 50.f,
                                           Shard::Layers::All, Owner());
        if (hit && hit->actor == target) {
            hit->actor.AddImpulse(Shard::Vec3{ 0.f, 5.f, 0.f });
            Shard::Audio::PlayOneShot("sounds/shot.wav", Owner().WorldPosition());
        }
    }

    void OnContactAdded(const Shard::Contact& contact) override
    {
        if (contact.other.HasTag("bullet")) Owner().Destroy();
    }
};
SHARD_REGISTER_SCRIPT(Turret, (target)(note))
```

* A `Shard::Actor` field is saved as the path of the actor (`Level/Enemy`). If that actor is not in the world yet when the script starts, the script waits and the field is filled before the first callback where the actor exists.
* The actor needs a `physics_body` component for `AddForce`, `AddImpulse`, `Velocity`, `SetVelocity` and the rest. A force lasts for one step, so add it in `OnFixedTick`.
* `Shard::Worlds::Load("worlds/arena.world")` replaces the loaded worlds at the start of a later frame, not in the middle of your callback. `GetScene().SpawnPrefab("crate")` builds `prefabs/crate.json` from your plugin's content, or from any module by its `mod://` address.
* To add a window to the editor, write a panel. In the player and in headless runs nothing is added and nothing else changes :

```cpp
SHARD_EDITOR_PANEL(Spawner, "Enemy spawner", ui)
{
    static int count = 5;
    ui.SliderInt("Count", count, 1, 50);
    if (ui.Button("Spawn")) SpawnEnemies(count);
}
```

The panel is listed in the editor's Window menu.

## 9. More of the SDK

* `ctx.CVars()`, `ctx.Commands()`, `ctx.Events()`, `ctx.Hooks()`, `ctx.Services()`, `ctx.Content()`, `ctx.Extensions()` : see [Plugins](../concepts/plugins.md#the-c-abi-and-the-sdk) and the [C ABI reference](../api_reference.md).
* `OnPrepareReload` and `OnRestore` of the plugin keep values across a hot reload in the editor.
* A component block is 1024 bytes at most. Anything bigger goes in a `Shard::Blob` member (`ctx.Components().CreateBlob(size)`, `BlobData(blob)`), and the actor component saves its bytes with the world. A script object can be up to 16 KB and can hold any member that is not a field.
* A `Shard::AssetId` member is an asset chosen in the inspector.
* `ctx.Events().Describe<Payload>("name")` tells the tools what an event carries, so `event.watch <name>` in the console prints each one with its fields.
* `Shard::experimental::World` reaches the engine's entities from the main thread, outside systems. It may change in any engine revision, and a signed plugin cannot use it.

If the engine refuses a registration, the SDK throws `Shard::Error`. If nothing catches it, the plugin fails and the message is in the log. No exception crosses into the engine : an exception in a script callback is logged and counted as an error of the plugin, and the game continues.

To write a plugin in C instead, include `<shard/shard.h>` and fill the tables yourself. `tests/plugin_fixtures/c_plugin/c_plugin.c` in the engine's repository uses every interface.

## 10. Ship it

The template asks for `PACKAGE`, so `cmake --build my-game/build --target template_plugin_package` writes `my-game/build/plugins/<id>.shardplugin` using the SDK's `bin/ShardPkg` (`SHARD_PKG_EXECUTABLE` selects another one). The archive contains the manifest, the library and the content. To sign it, see [Packaging, signing and registries](../concepts/plugins.md#packaging-signing-and-registries). A signed plugin cannot use the experimental interfaces.

A plugin built with SDK 1.x loads in every engine of ABI 1.x whose minor revision is at least the SDK's. To reach more engines, build against the oldest SDK that has what you use.
