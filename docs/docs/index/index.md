# Welcome

<p align="center">
  <img id="shard-logo" src="../images/ShardLogoWhite.png" width="20%" alt="Shard logo">
</p>

<script>
  function updateLogo() {
    const scheme = document.body.getAttribute('data-md-color-scheme');
    const logo = document.getElementById('shard-logo');
    if (!logo) return;

    logo.src = scheme === 'slate'
      ? '../images/ShardLogoBlack.png'
      : '../images/ShardLogoWhite.png';
  }

  // Initial update on page load
  window.addEventListener("load", updateLogo);

  // Update on theme change
  new MutationObserver(updateLogo).observe(document.body, {
    attributes: true,
    attributeFilter: ['data-md-color-scheme']
  });
</script>

Shard is a 3D game engine written in C++17 for Windows and Linux (desktop only). It is split into separate modules, so a project only pulls in what it uses, and the whole source is open.

---

## What is Shard?

The engine is built around three ideas:

- **Modular**: rendering, physics, audio and input are separate modules.
- **Fast**: aimed at real-time games and simulations.
- **Open**: the source is available and meant to be modified.

!!! warning "Alpha release notice"
    **Shard Engine** is in **active development**. An alpha version is expected in **November 2026**. Contributions, feedback and testing are welcome.

---

## Features

| Area                       | Highlights                                                                                                            |
|----------------------------|-----------------------------------------------------------------------------------------------------------------------|
| **Rendering**              | OpenGL core backend, PBR materials, dynamic lights, probe-based global illumination, path-traced reference renders    |
| **Physics**                | Rigid bodies and collisions powered by [Jolt](https://github.com/jrouwe/JoltPhysics)                                  |
| **Audio**                  | 3D positional audio through [FMOD](https://www.fmod.com/)                                                             |
| **Input**                  | Keyboard, mouse and gamepads (with dead zones and rumble)                                                             |
| **Assets**                 | FBX import ([ufbx](https://github.com/ufbx/ufbx)), reflection-based JSON serialization                                |
| **Platform**               | A dedicated [Platform Abstraction Layer](../platform.md): windowing, filesystem, threads, networking, crash reporting |
| **Editor**                 | An ImGui editor with scene hierarchy, gizmos and thumbnails, plus a hot-loadable game module                          |

## Quick start

**Requirements:** CMake 3.28.2+, a C++17 compiler (GCC MinGW recommended on Windows) and the FMOD Core API 2.03.14.

```bash
# Build everything: submodules, engine, editor and game module
./build.sh        # or build.bat on Windows

# Run the editor with the example project
./ShardEditor.exe --game plugins/game/bin/windows-x86_64/libGameModule.dll --project ../example_project/example_project.json --api opengl
```

The full setup (fonts, FMOD, resource folders, Linux packages) is described in [Getting Started](getting-started.md).

---

## Documentation overview

- [Getting Started](getting-started.md): install the engine and build a first game.
- [Core Concepts](../core-concepts.md): objects, scene management, input, rendering.
- [Modules](../modules.md): how rendering, audio, physics and scripting work.
- [Platform Abstraction Layer](../platform.md): OS and window system code, gamepads, Linux build notes.
- [API Reference](../API-reference.md): public classes and methods.
- [Tutorials](../tutorials.md): step-by-step guides for common gameplay mechanics.
- [Contributing](../contributing.md): how to contribute to the engine.

---

## Navigation

Use the sidebar to browse topics or the search bar to find a page.

Found a bug or need help? Open an issue on [GitHub](https://github.com/Windokk/Shard/issues).

---

*Shard Engine is an open-source project licensed under GNU GPL v3.0.*
