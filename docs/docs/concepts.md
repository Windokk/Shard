# Concepts

How the engine works, by topic.

## Engine

| Page | What it explains |
|---|---|
| [Modules](concepts/modules.md) | what the engine is made of, and what a module is |
| [Plugins and modules](concepts/plugins.md) | extending the engine : plugins, datapacks, the C ABI and the SDK, packaging and signing, hot reload |
| [Platform Abstraction Layer](concepts/platform.md) | the only code that talks to the OS or the window system |
| [Event system](concepts/events.md) | how parts of the engine and of a game tell each other that something happened |
| [Time](concepts/time.md) | how the engine measures time, and how a script uses it to behave the same at any frame rate |
| [Tags](concepts/tags.md) | hierarchical names on worlds, actors and components |

## Rendering

| Page | What it explains |
|---|---|
| [Renderer](concepts/renderer.md) | the OpenGL and Vulkan backends, the render graph, shaders, shadows, reflection probes |
| [Display](concepts/display.md) | linear lighting, tonemapping and sRGB |
| [Baked lighting](concepts/lightmaps.md) | lightmaps and how to bake them |

## World and simulation

| Page | What it explains |
|---|---|
| [Large world](concepts/large-world.md) | 64-bit coordinates and reference frames |
| [Gravity fields](concepts/gravity.md) | gravity as world data |
| [Headless mode and determinism](concepts/headless.md) | running with no window, replays, dedicated servers |

## Executables

| Page | What it explains |
|---|---|
| [Editor](concepts/executables/editor.md) | the editor and its panels |
| [Player](concepts/executables/player.md) | the runtime that plays a project |
