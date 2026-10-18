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
| `deferred` | Opaque and masked geometry is written to a G-buffer and lit in one fullscreen pass. Translucent geometry and the sky are still drawn forward. |

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

## Reflection probes

A **Reflection Probe** (*Add Component > Reflection Probe*) is a box of the world whose surfaces reflect a picture of what surrounds its actor, instead of the sky : indoors, a glossy surface reflects the walls of the room, not a sky it cannot see. Without a probe the reflection is the world's skybox, filtered by roughness.

*Bake* path traces the whole sphere around the actor with the offline raytracer (so the picture holds the lights, the bounces and the sky as the world has them) and saves it as an equirectangular `.hdr` in the project's `reflection_probes` folder, referenced by the world file like the baked GI probes. The picture is then loaded as a skybox would be : a cubemap, and a prefiltered cubemap with one level per roughness. `reflection.bake` and `reflection.status` do the same from the console.

| Field | Default | Meaning |
| --- | --- | --- |
| Half Extent | 5, 3, 5 | The box the probe covers, centered on the actor (it does not follow the actor's rotation or scale). |
| Bake Resolution / Samples / Bounces | 1024 / 256 / 4 | Width in pixels of the picture (the height is half), samples per pixel, light bounces. |
| Intensity | 1 | Multiplier on the light of the reflection. |
| Blend Distance | 1 | Distance inside the edge of the box over which the reflection fades into the sky's. |
| Box Projection | on | Corrects the lookup for the size of the box, so the reflection of a wall lands where the wall is. Turn it off for an open area. |

Only a material with *useEnvReflections* reflects a probe. **One probe is used at a time**, the one nearest to the camera (the smaller box when the camera is inside several) : `lit.frag` has no texture unit left for a second, it sits on the 32 samplers some drivers allow a fragment stage.

## Screen-space reflections

*World Settings > Screen-Space Reflections* (`ssr` in the world file, off by default) makes a shiny surface reflect what the screen shows where its mirror ray lands, in place of the reflection probe's or the sky's. What the screen does not show falls back to them : a ray that leaves the screen, goes behind an object or finds nothing weighs 0. Only a material with *useEnvReflections* takes it, like the probe and the sky. Same shaders on OpenGL and Vulkan.

It is a chain of compute passes, the `SSRPass` of the render graph (after the SSAO depth + normal prepass, which it reads, and before the lighting) :

1. **Hi-Z** : a pyramid of the closest depth of the prepass.
2. **Colour** : the previous frame's HDR picture and its mips (the blur of a rough reflection). The scene is lit after the chain, so the colour a reflection shows is the last frame's, looked up where the camera had it.
3. **Trace** : the hierarchical march of every pixel's mirror ray through the Hi-Z, at 1, 1/2 or 1/4 of the screen, blended with the frame before's hit (reprojected, world space) when *Temporal* is on.
4. **Resolve** : one entry per pixel (edge-aware upsample) : where the hit is in the previous picture, how wide the specular lobe is there, and a confidence.

The prepass has no materials, so the trace does not know the roughness : it is the mirror ray. `lit.frag` applies the roughness where it is known, by the level of the colour pyramid it reads (`roughness^2` wide) and by fading the reflection out of the surfaces rougher than *Max Roughness*. This means a GGX importance sampled trace (and the noise, and the denoiser it needs) is not what this is, and that the cost does not depend on the roughness of the pixels.

| Field | Default | Meaning |
| --- | --- | --- |
| Quality | Medium | A preset of Max Steps, Resolution and Temporal (Low 32 / 1/4, Medium 48 / 1/2, High 64 / 1/2, Ultra 96 / 1). Touching one of them makes it Custom. |
| Max Steps / Resolution / Temporal | 48 / Half / on | `maxSteps`, `resolutionDivisor` (1, 2, 4), `temporal`. |
| Temporal Blend | 0.85 | How much of the previous frame stays. Lowered by itself while the camera moves. |
| Intensity | 1 | How much of the reflection replaces the probe / sky one where a ray hit. |
| Max Distance | 50 | How far a ray travels, in world units. |
| Thickness | 0.3 | How far behind a surface of the depth buffer a ray may pass and still hit it. |
| Max Roughness | 0.6 | Beyond it the surface is left to the probes and the sky (the fade starts at 60% of it). |
| Roughness Blur | 1 | Scales the blur of a rough reflection. |
| Edge Fade | 0.1 | Fraction of the screen from its edges over which a reflection fades out. |

Orthographic cameras and the studio views (material previews) have no SSR. The textures are the size of the viewport (Hi-Z, colour pyramid, result and the two histories : about 260 MB at 4K on Half, 65 MB at 1080p), and the `SSR Hi-Z`, `SSR Colour`, `SSR Trace` and `SSR Resolve` GPU scopes show in the profiler.

## Texture filtering

Textures that have mips are sampled with anisotropic filtering, 16x by default (`render.anisotropy`, 1 to 16, capped by the GPU). It is read when a texture is loaded, so a texture already in memory keeps its level until it is loaded again.

## Debugging

- Passes are labelled for RenderDoc with `DebugScope`. When RenderDoc is attached, the engine uses its in-app API (`render.renderdoc`, `render.capture`).
- `render.dump <dir>` saves every pass target as a PNG, along with the graph.
- `SHARD_VULKAN_VALIDATION=1` turns on the validation layers.

## Editor on Vulkan

`ShardEditor --api vulkan` draws Dear ImGui to the swapchain with `imgui_impl_vulkan` (see `RendererAPI::PresentUI`). Textures reach ImGui as descriptor sets created on the render thread.
