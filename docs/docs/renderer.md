# Renderer

Shard renders with OpenGL 4.6 (default) or Vulkan 1.3. Both backends use the same shaders and produce the same image. Select one with `--api opengl|vulkan` (player, editor, headless) or the `"api"` field of the project.

## Requirements

Nothing to install. The Vulkan headers, volk, vk-bootstrap, VMA, glslang and SPIRV-Reflect are submodules, and the Vulkan runtime comes with your GPU driver.

You only need the Vulkan SDK for:

- the validation layers (`SHARD_VULKAN_VALIDATION=1`)
- `slangc`, to compile Slang shaders
- `vulkaninfo` and the other SDK tools

## Render graph

Each pass declares what it reads and writes (`RenderPass::graphAccesses` and its target). `RenderGraph` (in `renderer/rhi/graph`) uses that to work out:

- the order of the passes
- which passes can be skipped because nobody uses their output
- the barriers needed between two uses of a resource
- which transient targets can share memory

The graph is independent of the graphics API and has its own unit tests.

- `render.graph [file.dot]` prints the current frame's graph, and can write it as a Graphviz file.
- A pass that names the passes it runs after reads what they wrote. This is how ping-pong targets work.

## Shaders

Shaders are written in GLSL 4.3. For Vulkan, `ShardShaderCompiler` compiles them with glslang using the relaxed Vulkan rules:

- loose uniforms are collected into one uniform block
- resources are assigned a descriptor set by kind: 0 textures, 1 storage buffers, 2 storage images, 3 uniforms, 4 bindless textures
- `sampler2D(handle)` reads from the bindless array

SPIRV-Reflect runs on the output. A compiled program is saved as a `.spvpack` (SPIR-V plus reflection data), keyed on a hash of the sources, defines and compiler version, so a stale file is never loaded.

```
ShardShaderc <shader root> <output dir> [--define NAME[=VALUE]] [--only name] [--reflect] [--force] [-j n]
```

`--define` builds a permutation. At runtime `Shader::GetVariant` does the same, for example for the G-buffer and deferred variants of `lit.frag`.

### Slang

`.slang` files work on Vulkan only. They are compiled with `slangc` (from the Vulkan SDK, or the path in `SHARD_SLANGC`), with `vertexMain`, `fragmentMain` and `computeMain` as entry points.

Uniforms that the engine sets by name go in the constant buffer at `[[vk::binding(0, 3)]]`. Textures are `Sampler2D`. See `shaders/examples/slang_tint.slang`.

On Vulkan, `Shader::Create(file, file)` loads a Slang shader directly, and shaderc precompiles them (skipped when `slangc` isn't found). Materials can't use Slang shaders yet, and there is no Slang compute asset.

## Render pipelines

A render pipeline asset defines how a frame is built. They live in `resources/engine_resources/pipelines/*.renderpipeline`. Pick one with `"renderPipeline"` in the project, or switch at runtime with `render.pipeline <name>`.

| Pipeline   | Description                                                                                                   |
|------------|---------------------------------------------------------------------------------------------------------------|
| `forward+` | Default. Meshes are lit while drawn, using clustered lights, a depth prepass and MSAA.                         |
| `deferred` | Opaque and masked geometry is written to a G-buffer and lit in one fullscreen pass. Translucent geometry and the sky are still drawn forward. Geometry edges get no MSAA. |

To add one, implement `RenderPipeline` and register it with `RenderPipelineRegistry`.

## Debugging

- Passes are labelled for RenderDoc with `DebugScope`. When RenderDoc is attached, the engine uses its in-app API (`render.renderdoc`, `render.capture`).
- `render.dump <dir>` saves every pass target as a PNG, along with the graph.
- `SHARD_VULKAN_VALIDATION=1` turns on the validation layers.

## Editor on Vulkan

`ShardEditor --api vulkan` draws Dear ImGui to the swapchain with `imgui_impl_vulkan` (see `RendererAPI::PresentUI`). Textures reach ImGui as descriptor sets created on the render thread.
