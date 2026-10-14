# Renderer

Shard can render with OpenGL 4.6 (the default) or Vulkan 1.3. Both backends use the same shaders and draw the same thing. Pick one with `--api opengl|vulkan` (player, editor, headless) or with the `"api"` field of the project.

## Requirements

There's nothing extra to install. The Vulkan headers, volk, vk-bootstrap, VMA, glslang and SPIRV-Reflect are all submodules, and the Vulkan runtime ships with your GPU driver.

The Vulkan SDK is only needed if you want:

- the validation layers (`SHARD_VULKAN_VALIDATION=1`)
- `slangc`, to compile Slang shaders
- `vulkaninfo` and the other SDK tools

## Render graph

Each pass declares what it reads and writes (`RenderPass::graphAccesses` plus its target). From that, `RenderGraph` (in `renderer/rhi/graph`) figures out:

- the order the passes run in
- which passes can be skipped because nothing uses their output
- the barriers needed between two uses of a resource
- which transient targets can share the same memory

The graph doesn't depend on any graphics API and has its own unit tests.

- `render.graph [file.dot]` prints the current frame's graph, and optionally writes it as a Graphviz file.
- If a pass names the passes it runs after, it reads what they wrote. That's how ping-pong targets work.

## Shaders

The shaders are still written in GLSL 4.3. For Vulkan, `ShardShaderCompiler` compiles them with glslang using the relaxed Vulkan rules:

- loose uniforms are gathered into a single uniform block
- resources get a descriptor set based on their kind: 0 for textures, 1 for storage buffers, 2 for storage images, 3 for uniforms, 4 for bindless textures
- `sampler2D(handle)` reads from the bindless array

SPIRV-Reflect is run on the output. A compiled program is saved as a `.spvpack` (SPIR-V plus reflection data). The file is keyed on a hash of the sources, the defines and the compiler version, so an outdated one is never loaded.

```
ShardShaderc <shader root> <output dir> [--define NAME[=VALUE]] [--only name] [--reflect] [--force] [-j n]
```

`--define` builds a permutation. `Shader::GetVariant` does the same thing at runtime, for example for the G-buffer and deferred lighting variants of `lit.frag`.

### Slang

`.slang` files only work on Vulkan. They're compiled with `slangc` (from the Vulkan SDK, or wherever `SHARD_SLANGC` points) and use `vertexMain`, `fragmentMain` and `computeMain` as entry points.

Uniforms that the engine sets by name go in the constant buffer at `[[vk::binding(0, 3)]]`. Textures are `Sampler2D`. There's an example in `shaders/examples/slang_tint.slang`.

On Vulkan, `Shader::Create(file, file)` loads a Slang shader directly, and shaderc precompiles them (it skips them if `slangc` isn't found). Materials can't reference Slang shaders yet, and there's no compute asset for them either.

## Render pipelines

How a frame gets built is defined by a render pipeline asset. They live in `resources/engine_resources/pipelines/*.renderpipeline`. You choose one with `"renderPipeline"` in the project, or switch at runtime with `render.pipeline <name>`.

| Pipeline   | Description                                                                                                   |
|------------|---------------------------------------------------------------------------------------------------------------|
| `forward+` | Default. Meshes are lit as they're drawn, with clustered lights, a depth prepass and MSAA.                   |
| `deferred` | Opaque and masked geometry goes into a G-buffer and is lit in one fullscreen pass. Translucent geometry and the sky are still drawn forward. Geometry edges get no MSAA. |

To add a pipeline, implement `RenderPipeline` and register it with `RenderPipelineRegistry`.

## Debugging

- Passes are labelled for RenderDoc with `DebugScope`. When RenderDoc is attached, the engine uses its in-app API (`render.renderdoc`, `render.capture`).
- `render.dump <dir>` saves every pass target as a PNG, along with the graph.
- `SHARD_VULKAN_VALIDATION=1` enables the validation layers.

## Editor on Vulkan

`ShardEditor --api vulkan` draws Dear ImGui to the swapchain using `imgui_impl_vulkan` (see `RendererAPI::PresentUI`). Textures are passed to ImGui as descriptor sets, created on the render thread.
