# Event system

How parts of the engine and of a game tell each other that something happened, without knowing each other.

The `Events::EventDispatcher` (`engine/world/event_system.hpp`) is a typed publish / subscribe bus, reached with `Core::GetEngine().GetEventDispatcher()`. An event is a plain struct deriving from `Events::Event` ; a subscriber receives exactly the type it subscribed to.

## Scopes

An event is emitted to one scope, and only the subscribers of that scope hear it :

| Scope | Subscribe | Emit | Heard by |
|---|---|---|---|
| Global | `subscribeGlobal<T>(cb)` | `emitGlobal(e)` | everything listening to the type |
| World | `subscribeToWorld<T>(worldID, cb)` | `emitToWorld(worldID, e)` | the listeners of one world |
| Actor | `subscribeToActor<T>(actorID, cb)` | `emitToActor(actorID, e)` | the listeners of one actor |
| Component | `subscribeToComponent<T>(componentID, cb)` | `emitToComponent(componentID, e)` | the listeners of one component |

Scopes do not cascade : an event emitted to an actor is not also sent to its world or to the global listeners. Emit it twice if both should hear it.

## Using it

```cpp
auto* events = Core::GetEngine().GetEventDispatcher();

// Listen
events->subscribeGlobal<Events::WorldStructureChangedEvent>(
    [](const Events::WorldStructureChangedEvent& e) {
        if (e.changeType == Events::LOADED)
            OnWorldLoaded();
    });

// Send
events->emitGlobal(Events::WorldStructureChangedEvent(worldAssetID, Events::CREATED, "Door", sourceID));
```

## Writing your own event

Derive from `Event`, which carries the `sourceObjectID` of whoever sent it :

```cpp
struct DoorOpenedEvent : public Events::Event {
    float openAmount;
    DoorOpenedEvent(float amount, Core::ObjectID source) : Event(source), openAmount(amount) {}
};
```

Subscribers are found by the exact C++ type (`typeid`), so an event deriving from another is *not* delivered to the listeners of its base.

## Rules to know

* **Synchronous.** `emit` calls every handler, in subscription order, before it returns. There is no queue : a slow handler slows the sender, and a handler that emits runs the nested handlers inside itself.
* **No unsubscribe.** A subscription lives as long as the dispatcher. A handler that captures `this` must not outlive the object : subscribe from something that lives for the whole run (the editor's main window does), or check your own state in the handler.
* **Not thread-safe.** Subscribe and emit from the main thread, outside parallel systems.
* **Don't change the subscriptions of a type while it is being emitted** (subscribing in a handler of the same event), the handler list may move.
* The event is passed by `const&` and only valid during the call : copy what you need.

## Events the engine emits

| Event | Emitted | Carries |
|---|---|---|
| `WorldStructureChangedEvent` | globally, when an actor is created, destroyed, moved, activated or deactivated, and when a world is loaded or unloaded | `worldAssetID`, `changeType` (`CREATED`, `DESTROYED`, `MOVED`, `ACTIVATED`, `DEACTIVATED`, `LOADED`, `UNLOADED`), `actorName` |
| `TagsChangedEvent` | to the world (its asset id), the actor or the component (its id in the world) whose tags changed, in a loaded world. Not when a world is loaded | `added`, `removed` (the explicit tags that changed). See [Tags](tags.md) |

### Input events

Every OS input event is also sent globally, as it is reported, during the `Input` phase (so before the frame's scripts run). The input state read through `IInput` is already up to date when a handler runs. Events carry no source object (`ObjectID(0)`).

| Event | Carries |
|---|---|
| `KeyPressedEvent` / `KeyReleasedEvent` | `keyCode` (an `Input::Key`, or `key()`), and `repeated` for a press made by the OS key repeat |
| `TextInputEvent` | `text` (UTF-8), layout and modifiers applied : for text fields, not for bindings |
| `MouseButtonPressedEvent` / `MouseButtonReleasedEvent` | `button`, `x`, `y` in the window |
| `MouseMovedEvent` | `x`, `y`, `deltaX`, `deltaY` (the delta is still reported while the cursor is captured) |
| `MouseScrolledEvent` | `deltaX`, `deltaY` |
| `GamepadConnectedEvent` / `GamepadDisconnectedEvent` | `deviceId` |
| `GamepadButtonPressedEvent` / `GamepadButtonReleasedEvent` | `deviceId`, `button` |
| `GamepadAxisEvent` | `deviceId`, `axis`, `value` : raw, with no dead zone, on every change |

```cpp
events->subscribeGlobal<Events::KeyPressedEvent>([](const Events::KeyPressedEvent& e) {
    if (e.key() == Input::Key::E && !e.repeated)
        Interact();
});
```

Use the events to react to a change (a press, a plug), and the polled state (`IsKeyDown`, `GetGamepadAxis`, ...) for what is held. Deterministic runs and replays emit them too, as they come from the same OS events.

## Script callbacks are not dispatcher events

The contact events of physics (`ContactAddedEvent`, `ContactPersistedEvent`, `ContactRemovedEvent`, in `engine/physics/physics_events.hpp`) reuse the `Event` base, but they are not sent through the dispatcher : they are handed straight to the scripts of the actors involved, as the `OnContactAdded`, `OnContactPersisted` and `OnContactEnded` overrides of `Script`. They are delivered after the physics step, on the thread that ran it ; a script may read and change bodies, or destroy actors, there.

The other lifecycle hooks of a script (`OnCreate`, `OnPlay`, `OnTick`, `OnStop`, `OnActivated`, `OnDeactivated`, `OnDestroyed`, `OnWorldLoaded`, `OnWorldUnloaded`) are plain virtual calls too. Use the dispatcher when the sender and the receiver do not know each other ; use the callbacks for what concerns the script's own actor.

See also : [Time](time.md) for when in the frame each of these runs.
