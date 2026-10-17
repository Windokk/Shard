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

## Shadows

The shadow settings are in *Project Settings > Rendering*, in the *Shadows*, *Directional Shadows*, *Spot Shadows* and *Point Shadows* groups. They are saved in `rendering.shadows` in the project file and applied at once (the shadow maps are rebuilt when a resolution, the number of cascades, *Double-sided Casters* or *Conservative Rasterization* changes; everything else takes effect on the next frame).

The suns share 9 cascade map slots, and each takes one per cascade : with 3 cascades 3 suns cast shadows, with 4 or 5 only 2, with 6 only 1. The suns beyond that are unshadowed.

Resolutions are the side of a square map in texels. A sun has one map per cascade (three by default), so its memory is the number of cascades times the square of its resolution.

| Setting | Key | Default | |
|---|---|---|---|
| Enable Shadows | `enabled` | on | Off, every light is unshadowed. |
| **Directional** | | | |
| Resolution | `directionalResolution` | 2048 | Side of each cascade's map. |
| Distance | `directionalDistance` | 0 | How far from the camera the sun's shadows reach, in world units. 0 follows the camera's far plane. |
| Cascades | `cascadeSplits`, `cascadeColors` | 3 cascades (splits 0.27, 0.54; red, green, blue) | Edited as a bar : drag a handle to move the split between two cascades, right-click it to merge them, *Split* / *Remove* add or drop one. `cascadeSplits` holds where each cascade but the last ends, as a fraction (0 to 1, ascending) of the range from the camera's near plane to *Distance* ; `cascadeColors` holds one `[r, g, b]` per cascade, shown in the Shadow Cascades debug view. 1 to 6 cascades. |
| Stabilize Cascades | `stabilizeCascades` | on | Snaps each cascade to its texels so shadows don't shimmer when the camera moves. |
| Caster Range | `casterRange` | 500 | How far from a cascade, towards the sun, casters are still drawn. A caster further than this casts no shadow on it. |
| Double-sided Casters | `directionalDoubleSided` | on | Draws both faces into the sun's maps. With it off, only the faces turned away from the sun are drawn, which lets light through the edges of single-surface roofs and walls. |
| Conservative Rasterization | `directionalConservativeRaster` | on | Draws every texel a caster touches, so a gap thinner than a texel (the joints of a model) doesn't let a line of light through. OpenGL with `GL_NV_conservative_raster` only. |
| Normal Bias | `directionalNormalBias` | 2 | Texels the lit point is moved along the normal before the lookup, more on a surface that grazes the light. |
| Depth Bias | `directionalDepthBias` | 0.75 | Constant depth offset, in texels. |
| Slope Bias | `directionalSlopeBias` | 1 | Scale of the receiver plane bias that follows the surface's slope across the filter. |
| Filter Radius | `directionalFilterRadius` | 2 | The filter takes (2 × radius + 1)² taps. 0 is a single hardware filtered tap. |
| Filter Spread | `directionalFilterSpread` | 1 | Distance between two taps, in texels. |
| **Spot** | | | |
| Resolution | `spotResolution` | 1024 | Side of a spot light's map. |
| Depth Bias / Minimum Depth Bias | `spotDepthBias` / `spotMinDepthBias` | 0.0005 / 0.00005 | Slope scaled bias and its floor, in depth units of the map. |
| Filter Radius / Spread | `spotFilterRadius` / `spotFilterSpread` | 2 / 1 | Same as the sun's filter. |
| **Point** | | | |
| Resolution | `pointResolution` | 1024 | Side of each of the six faces of a point light's cube map. |
| Depth Bias | `pointDepthBias` | 0.15 | In world units. |
| Filter Samples / Radius | `pointFilterSamples` / `pointFilterRadius` | 20 / 0.05 | Taps (1 to 20) spread on a disc of that radius, in world units at the surface. 1 is a hard edge. |

## Ambient

*World Settings > Ambient* sets `ambient_color` in the world file : an HDR `[r, g, b]` (a component can exceed 1) added to every surface, scaled by its albedo, on top of the lighting. Worlds saved with the older single number `ambient_intensity` load it as a grey. A material can override it with an `ambientColor` uniform (`[r, g, b]`).

## Debugging

- Passes are labelled for RenderDoc with `DebugScope`. When RenderDoc is attached, the engine uses its in-app API (`render.renderdoc`, `render.capture`).
- `render.dump <dir>` saves every pass target as a PNG, along with the graph.
- `SHARD_VULKAN_VALIDATION=1` turns on the validation layers.

## Editor on Vulkan

`ShardEditor --api vulkan` draws Dear ImGui to the swapchain with `imgui_impl_vulkan` (see `RendererAPI::PresentUI`). Textures reach ImGui as descriptor sets created on the render thread.
