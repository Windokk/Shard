# Baked lighting (lightmaps)

A lightmap holds the diffuse light a surface receives — the sky, the lights you ask to be baked, and everything they bounce off — computed once with the engine's path tracer and stored in a texture laid over the surface. In the editor and the player the forward pass reads it back, so a baked scene has soft, noise-free shadows, colour bleeding and ambient occlusion at a texel per few centimetres, for nothing per frame.

It sits on top of the real-time GI probes, not instead of them : the probes keep lighting the models that move, and still give the reflections (what the surroundings reflect) on the baked ones.

## The workflow

1. **Mark what is static.** Every model has *Static Lighting* (on by default) : it blocks and bounces the light of a bake, and gets a lightmap. Untick it for what moves — a baked shadow does not follow an object, and the lightmap of one baked in place stays behind. A model that is not static is lit by the probes and the real-time lights, and still receives shadows from nothing baked.
2. **Choose what the lights do.** Every light has a *Bake Mode* :

    | Mode | Direct light | Bounced light | Use it for |
    |---|---|---|---|
    | **Dynamic** (default) | real time | not baked | lights that move or change |
    | **Stationary** | real time (shadow maps, specular highlights) | baked | a light that stays put but should keep its highlights and be tweakable |
    | **Static** | baked into the lightmaps — not computed again at run time for a lightmapped surface | baked | the key lights of the scene : the cheapest and the best looking |

    A Static light is still lit in real time on the surfaces that have no lightmap (the moving models), so they match.

    *Source Size* gives a light a real emitter : the radius of the lamp in world units (point and spot lights), or the angular radius of the sun disc in degrees (directional lights, the real sun is 0.27). It turns the hard shadow of an ideal point into a penumbra, in the baker and in the offline raytrace. The real-time shadow maps ignore it.
3. **Bake.** *World Settings → Rendering → Lightmaps → Bake* (or the console command `lightmaps.bake`). The bake runs a slice per frame, with a progress notification, and the editor stays usable. Save the world afterwards : the world file keeps where each model's lightmap is, and the pages go to `lightmaps/<world>.lightmap` in the project.
4. **Iterate.** Change a light, move a static model, press *Re-bake*. *Delete* puts the world back on the probes.

## The settings

| Setting | |
|---|---|
| Texels per meter | Sharpness : the lightmap texels one meter of surface gets, on every model. 8 for soft light on large surfaces, 32 for a crisp shadow edge, 64+ for hero props. |
| Samples | Light paths traced per texel. 256 is a clean result, 1024+ a final one. |
| Bounces | 1 is the sky and the Static lights' direct light only ; each more adds the light the surfaces throw on each other. |
| Indirect intensity | A multiplier on everything but a Static light's own direct light (Unreal's "indirect lighting intensity"). 1 is physical ; 2–4 fills the shadows for a brighter, flatter look. |
| Denoise | An edge aware filter guided by the surfaces and by how noisy each texel is : it flattens the noise of an open wall and leaves a shadow edge alone. |
| Firefly clamp | Caps the brightness one path may bring back, so a rare very bright one is not a white speck. |
| Max page size, max resolution per model | The lightmaps are packed into pages (textures) of at most this size, 4 bytes a texel ; one model never gets more than the maximum. A model can also be given its own resolution (*Lightmap Resolution* on the model). |

The settings are stored in the world file, so a re-bake starts from the last one.

## How it works

* **Lightmap coordinates.** Every mesh gets a second set of texture coordinates when it is loaded (`mesh.lightmap_uvs`) : the surface is cut into charts of triangles that face about the same way, each chart is flattened and the charts are packed, all at the same scale, so a meter of surface takes the same number of texels everywhere. The cutting is deterministic, and cached in `<project>/.cache/lightmap_uv` (derived data, safe to delete) : a mesh of millions of triangles is only cut the first time it loads.
* **Layout.** The lightmaps of all the static models are given a size from the texel density and packed into an atlas (`PlanLightmapLayout`).
* **Baking.** The CPU draws every page into a G-buffer (the surface point and normal of each texel), the GPU traces paths from each texel (`lightmap_bake.comp`, sharing its path tracer with the offline raytrace) — one cosine weighted path for the indirect light, one shadow ray per Static light to a random point of its emitter —, the sums become a mean and a variance (`lightmap_resolve.comp`) and a five iteration a-trous filter (`lightmap_atrous.comp`) removes the noise. The page is then read back, its gutter filled (so bilinear filtering at the edge of a chart never reads black) and packed to 32 bit shared exponent texels.
* **Run time.** `lit.frag` reads the page by a bindless handle (every sampler unit is taken) and uses it instead of the probes' diffuse light and the sky's. The page is `GL_RGB9_E5` : half the size of RGBA16F, with no banding on a smooth gradient.

## Console

| Command | |
|---|---|
| `lightmaps.bake [samples] [texels per meter] [bounces] [max page size] [max resolution]` | bakes the first world's static models |
| `lightmaps.status` | the bake in progress, and the lightmaps the world uses |
| `lightmaps.clear` | deletes the baked lightmaps |
| `world.save` | writes the first world back to its file |
| `raytrace.render <file.hdr> [width] [height] [samples] [bounces]` | path traces the world from the active camera into a linear HDR file (with a `.png` preview next to it) |

`mesh.lightmap_uvs` (console variable, default on) turns the generation of the lightmap coordinates off ; a mesh loaded without them keeps the probe lighting.

## Limits

* Diffuse only : a Static light's specular highlights are not baked (use Stationary for highlights), and the reflections still come from the probes and the skybox.
* The light of a baked surface does not react to a change of a light or of the geometry before the next bake, and has no directional response to normal maps (the baked light is the light a smooth surface receives).
* One page costs `size² × 4` bytes in video memory, and the bake itself needs about `size² × 80` bytes while a page is traced. A level made of one huge mesh (the sample Sponza is 5.7 million triangles) wants a 4096 page ; a level cut into many static meshes bakes more sharply for the same cost.
* A bake needs the world's static models to have lightmap coordinates, and the world to be loaded. The BVH of the bake is built on the engine's job system : do not bake while a physics simulation runs on a big scene (play mode).
