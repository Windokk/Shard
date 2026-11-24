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

Some drivers allow only 32 samplers in a fragment shader. `lit.frag` uses 11 : the material's textures (or the G-buffer), the sky, the reflection probe, one array per kind of shadow map, and the emission map. The engine's other textures (AO, reflections, lightmap pages, GI probe atlases) are read through bindless handles. The layout is listed at the top of `lit.frag`.

SPIRV-Reflect runs on the output. A compiled program is saved as a `.spvpack` (SPIR-V plus reflection data), keyed on a hash of the sources, defines and compiler version, so a stale file is never loaded. For shaders inside a plugin folder (one that holds `shard-plugin.json`) the file is written to the user's cache directory instead, never next to the plugin, so the plugin's signature stays valid. A `.spvpack` placed there before signing is read first.

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
| `deferred` | Opaque and masked geometry is written to a G-buffer and lit in one fullscreen pass. With MSAA on, the G-buffer is multisampled and the pass lights every sample, so the edges are anti-aliased as in `forward+`. Translucent geometry and the sky are still drawn forward. |

To add one, implement `RenderPipeline` and register it with `RenderPipelineRegistry`.

The editor's view modes work with both pipelines. In `deferred`, the lighting pass draws the Normals, Depth, Unlit, GI, SSAO and Shadow Cascades views from the G-buffer. The G-buffer pass writes the UVs, Lightmap UVs and Wireframe colours in place of the albedo. The Shaded Wireframe lines come from one more sweep over the G-buffer's draws. To support the view modes in your own pipeline, set `RenderPass::debugViewRole` on its passes. A pipeline whose `SupportsView` returns false for a view falls back to `forward+` while that view is on.

## Shadows

The shadow settings are in *Project Settings > Rendering*, in the *Shadows*, *Directional Shadows*, *Spot Shadows* and *Point Shadows* groups. They are saved in `rendering.shadows` in the project file and applied at once (the shadow maps are rebuilt when a resolution, the number of cascades, *Double-sided Casters* or *Conservative Rasterization* changes; everything else takes effect on the next frame).

The suns share 9 cascade map slots, and each takes one per cascade : with 3 cascades 3 suns cast shadows, with 4 or 5 only 2, with 6 only 1. The suns beyond that are unshadowed.

Resolutions are the side of a square map in texels. A sun has one map per cascade (three by default), so its memory is the number of cascades times the square of its resolution.

The maps of each kind of light are the layers of one texture array : one for the suns' cascades, one for the spot lights, one cube map array for the point lights. `lit.frag` reads each array through a single sampler, so the number of shadowed lights does not use up sampler units. The arrays are sized to the lights that have a shadow slot, and are rebuilt when one is added beyond their size.

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
| Conservative Rasterization | `directionalConservativeRaster` | off | Draws every texel a caster touches (it costs about three times the time of a cascade on a dense level, hence off by default), so a gap thinner than a texel (the joints of a model) doesn't let a line of light through. Needs `GL_NV_conservative_raster` (OpenGL) or `VK_EXT_conservative_rasterization` (Vulkan) ; ignored on a GPU without it, and the two APIs then differ by a thin line of light. |
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

Only a material with *useEnvReflections* reflects a probe. **One probe is used at a time**, the one nearest to the camera (the smaller box when the camera is inside several).

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

Textures that have mips are sampled with anisotropic filtering, 16x by default (`render.anisotropy`, 1 to 16, capped by the GPU). It is read when a texture is loaded, so a texture already in memory keeps its level until it is loaded again. `rendering.maxAnisotropy` in the project file (set by the [quality presets](#quality-presets)) caps it : the smaller of the two applies.

## Quality presets

A quality preset is a named set of values for settings the project already has : MSAA, the shadow maps, SSAO and SSR, anisotropy, occlusion culling, and the physics solver and time step. There are five, **Ultra Low**, **Low**, **Medium**, **High** and **Ultra High**, and the engine can pick one for the machine it runs on. **High is exactly what a project had before the presets existed** (the defaults of *Project Settings > Rendering* and *Physics*), and a project that says nothing about quality is `custom` : its own values are used as they are.

### Choosing

| Choice | Meaning |
|---|---|
| `custom` | No preset : the values of the Rendering and Physics categories. The default. |
| `auto` | The preset recommended for this machine (below). |
| `ultra_low`, `low`, `medium`, `high`, `ultra_high` | That preset, whatever the machine. |

- In the editor, *Project Settings > Quality* has the *Quality Preset* list, what the engine found of the machine, the preset *Auto* would use and why, and the values in use. *Copy Into Project Settings* writes those values into the Rendering and Physics categories and goes back to Custom, to tune them by hand. While a preset is in use, the rows it covers in the Rendering category (and the time step and solver in Physics) are disabled and show the project's own values.
- In the project file : `"quality": { "preset": "auto" }`. It is saved with the project and shared by everyone who runs it.
- With the `quality.preset` variable : `project` (the default : defer to the project file) or one of the choices above. It is what a player sets, so it wins over the file : `ShardGame --project p.json +quality.preset=low`, a `quality.preset = low` line in `user.cfg` next to the executable, or the console while the game runs (the new preset is applied at once, and a value set from the console is written to `user.cfg` when the engine shuts down ; one from the command line is not). A name that is not a preset is ignored.
- The `quality` console command prints the selection, the machine, the recommendation and the values in use ; `quality presets` lists the names.

The preset overrides the values it covers **while the engine runs** and never rewrites the project's own : saving the project saves what the Rendering and Physics categories say, plus the choice.

### What a preset sets

| Setting | Ultra Low | Low | Medium | High | Ultra High |
|---|---|---|---|---|---|
| MSAA | off | off | 2x | 4x | 8x |
| Anisotropy cap (`maxAnisotropy`) | 1x | 4x | 8x | 16x | 16x |
| SSAO / SSR allowed (`ssao`, `ssr`) | off / off | off / off | on / off | on / on | on / on |
| Occlusion culling | on | on | on | on | on |
| Sun shadows : map size × cascades | 512 × 2 | 1024 × 2 | 1024 × 3 | 2048 × 3 | 4096 × 4 |
| Sun shadows : filter radius | 0 | 1 | 1 | 2 | 3 |
| Sun casters | single-sided | single-sided | single-sided | double-sided | double-sided |
| Conservative rasterization | off | off | off | off | on (if the GPU has it) |
| Spot shadow map / filter radius | 512 / 0 | 512 / 1 | 1024 / 1 | 1024 / 2 | 2048 / 3 |
| Point shadow map / filter samples | 512 / 4 | 512 / 8 | 1024 / 12 | 1024 / 20 | 2048 / 20 |
| Physics time step | project's × 2 | × 1.5 | × 1 | × 1 | × 0.5 |
| Velocity / position iterations, collision steps | 4 / 1, 1 | 6 / 1, 1 | 8 / 2, 1 | 10 / 2, 1 | 14 / 3, 1 |

What a preset does not cover keeps the project's value : the render pipeline, the biases, the shadow distance, the cascade splits when the project already has as many cascades as the preset asks for, the gravity.

- `ssao` and `ssr` are switches of the *project* (`rendering.ssao`, `rendering.ssr`, on by default) : a world turns SSAO and SSR on in its own settings, and a preset that switches them off wins over the world. With both off the SSAO depth and normal prepass, a second sweep over the scene, is skipped too.
- `maxAnisotropy` (`rendering.maxAnisotropy`, 16 by default) caps `render.anisotropy` : the smaller of the two applies, to the textures loaded afterwards.
- `velocitySteps`, `positionSteps` and `collisionSteps` (`physics.*`, Jolt's 10, 2 and 1) are the solver's iterations per step and the collision sub-steps of a step.
- MSAA off (`msaaSamples` 1) is accepted in the project file and in the editor's list (*Off*). The project file used to refuse anything under 2.
- **The simulation follows the preset only when it can.** In a deterministic run (one that records or replays), the time step and the solver are left as the project has them, so that every machine computes the same simulation ; the image still follows. Elsewhere, remember that a preset changes the physics too : a project whose gameplay needs the same simulation on every machine (lockstep multiplayer, for one) should use `custom`, or `high`, rather than `auto`.

### Choosing for the machine

`auto` reads the machine once, after the renderer starts : the GPU's name (`GL_RENDERER`, or the Vulkan device name), its dedicated video memory, the logical cores and the physical memory. The video memory comes from the graphics API (`GL_NVX_gpu_memory_info` on OpenGL, the device-local heap on Vulkan, nothing for an integrated GPU, whose heap is the system's), or else from the system (DXGI on Windows, `amdgpu` on Linux). `RecommendQuality` (`engine/assets/project/quality.hpp`) turns it into a preset in three steps :

1. **The kind of GPU.** A software renderer (`llvmpipe`, SwiftShader, Microsoft Basic Render Driver, a headless run) gets Ultra Low. An integrated GPU (Intel UHD, HD, Iris and Xe, the Radeon Graphics and Vega of an APU, Apple, Adreno, Mali) starts at Low.
2. **A discrete GPU is placed by its video memory** : under 1.5 GB Ultra Low, under 3 GB Low, under 7 GB Medium, under 15 GB High, and Ultra High above. The thresholds sit under the round sizes because the figures reported do (an 8 GB card shows 8151 MB on OpenGL and 7895 MB on Vulkan). When the memory is not known the answer is Medium. An older discrete card, whose memory flatters it (the GTX 9 and 10 series and before, the RX 400 and 500, Radeon R9 and HD), is capped at Medium.
3. **The system caps it.** Under 3.5 GB of RAM : Ultra Low ; under 7 GB : at most Low ; under 11 GB : at most Medium. Two logical cores or fewer : at most Low ; four or fewer : at most Medium. An unknown figure (0) caps nothing.

The memory is a crude measure of a GPU's speed, and the name lists are short on purpose : the recommendation is a starting point, which the *Quality* category and the `quality` command explain line by line (*"NVIDIA GeForce RTX 4090 : 24564 MB of video memory, starts at Ultra High"*, *"4 logical cores : capped at Medium"*). The test machines of `tests/assets/quality_tests.cpp` (a RTX 5060 gets High, a 4090 Ultra High, a GTX 1060 Medium, an Intel UHD Low, llvmpipe Ultra Low...) pin the heuristic down ; it needs no GPU to run, since the machine is a `HardwareProfile` that the test fills in. To change a threshold, change the constants at the top of `quality.cpp` and the test that names them.

### What they cost

`ShardGame --bench --bench-warmup 100 --frames 300 +render.gpu_timers=true +quality.preset=<name>` on Sponza (1.9 M triangles, the project's `deferred` pipeline) at 1920x1080, on an RTX 5060 with a Ryzen 7 9800X3D, in the `build/` of the repository ; the game is GPU-bound there (a Debug build gives the same figures). One run of each, vsync off ; a run varies by about 5 to 8% (the scene is dynamic), so two figures closer than that are the same.

| Preset | OpenGL fps | GPU frame | gain over `custom` | Vulkan fps | Render targets |
|---|---|---|---|---|---|
| `custom` (= High) | 74.1 | 13.4 ms | : | 83.3 | 671 MB |
| Ultra Low | 174.0 | 5.6 ms | x 2.3 | 203.7 | 249 MB |
| Low | 154.9 | 6.3 ms | x 2.1 | 199.4 | 255 MB |
| Medium | 97.4 | 10.2 ms | x 1.3 | 111.0 | 429 MB |
| High | 73.3 | 13.5 ms | x 1.0 | 89.9 | 671 MB |
| Ultra High | 34.3 | 29.1 ms | x 0.46 | 49.8 | 1290 MB |

`custom` and High agree, as they must : on this project they are the same settings (the figures differ by the noise). Where the time goes, in ms of GPU time on OpenGL :

| Pass | Low | Medium | High | Ultra High |
|---|---|---|---|---|
| G-buffer | 2.1 | 2.3 | 3.1 | 4.8 |
| Lighting (interior + edge pixels) | 2.2 | 3.4 | 5.0 | 7.5 |
| Sun cascades (each) | 0.8 | 0.8 | 1.0 | 3.0 to 4.2 |
| SSAO (prepass, raw, blur) | off | 1.5 | 1.6 | 1.6 |

- High to Low saves 7.2 ms : about 2.8 in the lighting (no samples to light at the edges), 1.6 for the SSAO (with SSAO and SSR off, the prepass that redraws the scene is skipped), 1.4 in the cascades (one less, and 1024 instead of 2048), 1.0 in the G-buffer and 0.15 in the MSAA resolve. Before the presets, switching MSAA off alone had taken this scene from 80 to 97 fps.
- Medium keeps 2x MSAA and the SSAO, and drops SSR, the second face of the sun casters, the cascade size and a ring of the filter : the "combo" measured before the presets, with 2x samples left.
- Ultra High costs more than twice High : 8x samples, a fourth cascade and the conservative rasterization (which tripled each cascade on this level). It is for a GPU that has time to spare : `auto` never picks it on this one.
- The video memory of the process went from 2.5 GB (Ultra Low) to 2.9 GB (High) and 3.5 GB (Ultra High) : the render targets and the shadow maps, the textures and the meshes are the same.
- The physics changes are not in these figures : the scene has few bodies, and the physics took 6 ms of CPU time per frame, in parallel with the GPU.

## Debugging

- Passes are labelled for RenderDoc with `DebugScope`. When RenderDoc is attached, the engine uses its in-app API (`render.renderdoc`, `render.capture`).
- `render.dump <dir>` saves every pass target as a PNG, along with the graph.
- `render.screenshot <file>` saves the last frame: a `.hdr` holds the scene in linear light, before exposure and tonemapping; a `.png` holds the picture as shown.
- `SHARD_VULKAN_VALIDATION=1` turns on the validation layers.

## Comparing against the path tracer

The offline path tracer (`raytrace.render`) is the reference the real-time lighting aims for. To find out which approximation is wrong, both renderers split the light leaving a surface into four terms (`rhi/lighting_terms.hpp`, `LIGHTING_TERM_*` in `light.glsl`):

| Term | What it is | Real time |
|---|---|---|
| `direct` | the lights, straight onto the surface, shadowed | clustered lights, shadow maps |
| `indirect_diffuse` | everything else arriving through the diffuse lobe: sky, bounces, emissive surfaces, ambient floor | GI probes, lightmaps, sky irradiance, SSAO |
| `indirect_specular` | everything else arriving through the specular lobe | environment map, reflection probes, SSR |
| `emission` | the surface's own light, and the sky where no surface is seen | emission, skybox |

- `render.lighting_terms <terms>` makes the main view draw only some terms (`all`, `none`, or names joined by `+`). While it is not `all`, SSR keeps the reflections of the last full frame, and the sky is drawn only with `emission`.
- `raytrace.render <file.hdr> <w> <h> <spp> <bounces> terms` also writes `<file>.<term>.hdr`. Every contribution of a path is tagged by the first surface it leaves (its own emission, the lights sampled from it, or the lobe the path continues by), so the term images add up exactly to the picture.
- `render.camera <px> <py> <pz> <tx> <ty> <tz> [fov]` draws the frames (and what `raytrace.render` traces) from another view: a position, the point it looks at, the vertical field of view. It leaves the simulation alone. `render.camera off` goes back to the active camera, and `render.camera` alone prints the view and the extent of the meshes.
- `python scripts/compare_frames.py reference --world sponza.world` runs both from the world's views in `scripts/reference_views.json` (or its camera) and writes `build/reference_compare/<world>/report.html`. For each term, over all views and for each view, it gives the term's share of the light, the overall bias in stops, the error per area (too bright / too dark), and an error map. With `--keep-reference`, only the real-time side is rendered again.

A lightmapped surface takes the direct light of `Static` lights from its lightmap, so in real time that light counts as `indirect_diffuse`. Compare with Dynamic or Stationary lights to keep the terms apart.

## Performance

`bash scripts/bench.sh --api vulkan --script my.txt` (with `my.txt` holding `0 render.pipeline deferred`, for example) prints the frame time, the GPU time of every pass and the memory. The figures below are Sponza at 1280x720 with 4x MSAA, 700 frames of which the first 120 are ignored, on an RTX 5060, in a Debug build of the engine. Only the GPU figures mean something in a Debug build (the CPU side is several times slower than in Release), and the player is held at about 120 fps by something in the frame loop (not looked into), so the frame rate says nothing here.

GPU time per frame, in ms, over three runs of each (a run made while another program was using the machine was left out : OpenGL took 23 to 31 ms then, Vulkan 6 to 7 ms) :

| | `forward+` | `deferred` |
|---|---|---|
| OpenGL | 8.2 to 8.5 | 7.0 (12.7 in one run) |
| Vulkan | 6.9 to 7.2 | 6.1 to 6.9 |

| Render targets (engine count) | `forward+` | `deferred` |
|---|---|---|
| OpenGL | 149 MB | 290 MB |
| Vulkan | 149 MB | 269 MB |

- Both pipelines cost about the same : `deferred` is at most a little cheaper, by the 5 to 15% the runs show, which is about the noise. Its G-buffer and lighting passes (1.5 and 3.2 ms on Vulkan) replace the depth sweep and the shaded sweep (0.75 and 5.0 ms).
- Vulkan is a little faster than OpenGL on the same scene (by about 1 ms for `forward+`), and a lot more stable when the machine is busy.
- The video memory the driver reports is 2.2 to 2.8 GB for all four, and varies from one run to the next (Vulkan `forward+` gave 2686 then 2238 MB) : it is no figure to compare. Before this was measured the OpenGL count of render targets ignored the extra colour attachments, so it gave 185 MB for the `deferred` G-buffer instead of 290 MB.
- The pictures of the four combinations agree to a mean error of 0.3 of 255 or less (see above).

## Editor on Vulkan

`ShardEditor --api vulkan` draws Dear ImGui to the swapchain with `imgui_impl_vulkan` (see `RendererAPI::PresentUI`). Textures reach ImGui as descriptor sets created on the render thread.

## Comparing OpenGL and Vulkan

`python scripts/compare_frames.py apis --world sponza.world` renders the world with `--api opengl`, then with `--api vulkan`, from the same views and the same seed, and reports how the pictures differ: mean error in levels of 255, RMSE, PSNR, percentiles of the pixel error, the share of pixels off by more than 1 to 32 levels, and the areas that differ most. `build/api_compare/<world>/diff.<view>.png` shows opengl | vulkan | difference map. `--max-mean`, `--max-p99` and `--max-over` make it exit with 1 (also when a picture is blank), and `compare_frames.py diff a.png b.png` compares any two pictures. `--a vulkan:forward_plus --b vulkan:deferred` compares two pipelines on one API. `bash scripts/parity_test.sh apis` does the comparison with limits, and the CI runs `parity_test.sh pipelines` on a software Vulkan. See `scripts/README.md`.

On Sponza the two APIs agree to a mean error of 0.15 to 0.3 of 255 (99% of the pixels within 3 levels), and so do the pipelines. The path tracer gives the same picture on both.
