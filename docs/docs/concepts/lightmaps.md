# Baked lighting (lightmaps)

A lightmap stores the diffuse light reaching a surface: sky light, the lights you choose to bake, and the light bouncing between surfaces. It is computed once with the engine's path tracer and saved in a texture mapped onto the surface. At runtime the forward pass only samples it. This gives soft, noise-free shadows, color bleeding and ambient occlusion at a few centimeters per texel, at no per-frame cost.

Lightmaps work next to the real-time GI probes and don't replace them. Moving models are still lit by the probes, and baked models still take their reflections from them.

## Workflow

1. **Mark static objects.** Every model has a *Static Lighting* option, on by default. Static models block and bounce light during the bake and get a lightmap. Turn it off for anything that moves: a baked shadow won't follow the object, and its lightmap would stay where it was baked. Non-static models are lit by the probes and the real-time lights.

2. **Set the light modes.** Every light has a *Bake Mode*:

    | Mode | Direct light | Bounced light | Use it for |
    |---|---|---|---|
    | **Dynamic** (default) | real-time | not baked | lights that move or change |
    | **Stationary** | real-time (shadow maps, specular highlights) | baked | lights that stay put but need highlights or runtime tweaks |
    | **Static** | baked (not computed at runtime on lightmapped surfaces) | baked | the main lights of the scene; cheapest and best-looking |

    Static lights are still applied in real time on surfaces without a lightmap (moving models), so both match.

    A Dynamic light bounces nowhere on lightmapped surfaces: the lightmap only holds the sky and the baked lights. For a sun that doesn't move, use Stationary, or its bounce light is missing (raising *Indirect intensity* to make up for it brightens the sky light everywhere instead).

    *Source Size* gives a light a physical size: the lamp radius in world units for point and spot lights, or the sun disc's angular radius in degrees for directional lights (the real sun is 0.27). Larger sources give softer shadows in the baker and the offline raytracer. Real-time shadow maps ignore it.

3. **Bake.** Go to *World Settings > Rendering > Lightmaps > Bake*, or run `lightmaps.bake` in the console. The bake is spread over several frames with a progress notification, so the editor stays usable. Save the world when it finishes: the world file stores each model's lightmap location, and the pages are written to `lightmaps/<world>.lightmap` in the project.

4. **Iterate.** After changing a light or moving a static model, press *Re-bake*. *Delete* removes the lightmaps and goes back to probe lighting.

## Settings

| Setting | |
|---|---|
| Texels per meter | Lightmap resolution, the same for every model. About 8 gives soft light on large surfaces, 32 gives sharp shadow edges, 64 or more suits hero props. |
| Samples | Paths traced per texel. 256 looks clean, use 1024 or more for final quality. |
| Bounces | 1 means only sky light and the direct light of Static lights. Each extra bounce adds light reflected between surfaces. |
| Indirect intensity | Multiplies everything except the direct light of Static lights, like Unreal's "indirect lighting intensity". 1 is physically correct, 2 to 4 brightens the shadows for a flatter look. |
| Denoise | Edge-aware filter using the surface data and each texel's noise level. It smooths flat areas and keeps shadow edges sharp. |
| Denoiser settings | Under *Denoise*. **Passes** (each one doubles the filter's reach), **Light tolerance** (how much light difference still counts as noise; higher is smoother but blurs soft shadows), **Normal sharpness** (how fast surfaces facing different ways stop mixing) and **Plane tolerance** (how far from the surface's plane a neighbour may be, in texels). Applied at the next bake. |
| Firefly clamp | Caps how bright a single path can be, so rare very bright samples don't show up as white dots. In addition, before denoising, a texel more than 2.5 times brighter than its brightest neighbour is brought down to that limit : a lone bright dot is a spike, a real edge of light always has a bright neighbour. If dots remain in a dark scene, lower the clamp (4 to 8). |
| Max page size, max resolution per model | Lightmaps are packed into pages (textures) no larger than this, at 4 bytes per texel, and no model goes above the max resolution. You can also override the resolution per model with *Lightmap Resolution*. |

Settings are saved in the world file, so the next bake reuses them.

## How it works

- **Lightmap UVs.** When a mesh loads, it gets a second UV set (`mesh.lightmap_uvs`). The surface is split into charts of triangles facing roughly the same way, each chart is flattened, and all of them are packed at the same scale so texel density is uniform. The process is deterministic and cached in `<project>/.cache/lightmap_uv` (safe to delete), so a huge mesh is only unwrapped once.
- **Layout.** Each static model gets a lightmap size from the texel density, and they are all packed into an atlas (`PlanLightmapLayout`).
- **Baking.** The CPU rasterizes each page into a G-buffer holding the position and normal of every texel. The GPU then traces paths from each texel (`lightmap_bake.comp`, which shares its path tracer with the offline raytracer): one cosine-weighted path for indirect light, and one shadow ray per Static light toward a random point on its emitter. The paths go through the back faces of single-sided materials, as the raster culls them (set *Double Sided* on the material if a surface should be seen from behind). Shadow rays stop on both faces, like the sun's shadow maps with the project's *Double-sided Casters*. The results become a mean and a variance (`lightmap_resolve.comp`), then are denoised with five iterations of an à-trous filter (`lightmap_atrous.comp`). Finally the page is read back, its gutters are filled so bilinear filtering never picks up black at chart edges, and it is packed into shared-exponent texels.
- **Runtime.** `lit.frag` samples the page through a bindless handle (all sampler units are already taken) and uses it in place of the diffuse light from the probes and the sky. Pages are stored as `GL_RGB9_E5`, half the size of RGBA16F and without banding on smooth gradients.

## Console

| Command | |
|---|---|
| `lightmaps.bake [samples] [texels per meter] [bounces] [max page size] [max resolution]` | bakes the static models of the first world |
| `lightmaps.status` | shows the current bake and the lightmaps used by the world |
| `lightmaps.clear` | deletes the baked lightmaps |
| `world.save` | saves the first world to its file |
| `raytrace.render <file.hdr> [width] [height] [samples] [bounces] [terms]` | path traces the world from the active camera to a linear HDR file, plus a `.png` preview ; `terms` also writes the light split by term (see the renderer's *Comparing against the path tracer*) |

The `mesh.lightmap_uvs` console variable (on by default) turns off lightmap UV generation. Meshes loaded without them fall back to probe lighting.

## Limitations

- Diffuse only. Specular highlights from Static lights aren't baked (use Stationary if you need them), and reflections still come from the probes and the skybox.
- Baked lighting doesn't follow changes to lights or geometry until you bake again. It also ignores normal maps, since it stores the light received by a flat surface.
- A page uses `size² × 4` bytes of VRAM, and baking needs about `size² × 80` bytes while a page is traced. A level made of one giant mesh (the Sponza sample has 5.7 million triangles) needs a 4096 page. Splitting a level into many static meshes gives sharper results for the same cost.
- Static models need lightmap UVs, and the world must be loaded before baking. The bake builds its BVH on the engine's job system, so avoid baking a big scene while physics runs (play mode).
