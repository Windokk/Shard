# Display: linear lighting, tonemapping and sRGB

Lighting math only works in linear space, but monitors expect sRGB and can't show anything brighter than white. So the engine renders the scene in linear HDR and only converts it for display at the end. There are exactly two conversion points:

```
sRGB textures --(decoded by the GPU when sampled)--> linear HDR scene --(tonemap pass)--> 8-bit sRGB image
```

## Pipeline

| Stage | Details |
|---|---|
| **Color textures** (*albedo, diffuse, emissive, basecolor* slots) | Stored as sRGB (`GL_SRGB8_ALPHA8`, BC7 sRGB), so the GPU converts them to linear when sampling and filters in linear space. Data textures (normal, metallic, roughness, AO) stay linear. The slot type is detected from the sampler name (`IsColorTextureSlot`). Mipmaps of color textures are generated in linear space, so a distant checkerboard averages to the right grey instead of something too dark. |
| **Sky** | Radiance `.hdr` files are loaded as floats (`RGB16F`), so the sun stays much brighter than the rest of the sky. 8-bit skies and 6-face cubemaps are decoded from sRGB. The irradiance and prefilter convolutions clamp samples to 500, so the sun disc can't overflow 16-bit floats or make the diffuse term flicker. |
| **Scene** | The forward pass renders to a multisampled `RGBA16F` target, so lighting can go well above 1 without clipping. |
| **Tonemap pass** (`shaders/fb/tonemap.frag`) | Resolves the scene, applies exposure, the tonemapper and the sRGB curve, adds a bit of dithering to avoid banding in skies and soft shadows, and writes the 8-bit viewport. |
| **Overlays** | Grid, outlines, billboards and debug shapes are drawn on top of the viewport after tonemapping, using their colors as-is. They depth-test against a copy of the scene depth. |

The viewport (`Renderer::GetViewportTextureHandle`) is still the final image, used by the editor, `--screenshot` and the player like before.

Thumbnails and material previews render directly to an 8-bit target, so `lit.frag` does the conversion itself in that case (`outputDisplayColors`, ACES, no exposure).

## Settings

In *World Settings > Display*. Saved under `display` in the world file, only if different from the defaults.

| Setting | |
|---|---|
| Tonemapper | `aces` (default, filmic curve), `neutral` (Khronos PBR Neutral: colors below ~0.8 are left alone, only highlights are compressed), `reinhard`, or `none` (just clamps, anything above white is white). |
| Exposure (EV) | Applied before the curve. +1 doubles the light, -1 halves it. |

Debug views handle this differently. *Lit* goes through the full transform. *Unlit* and *No lighting* show albedo, so they only get the sRGB curve. Normals, depth, UVs, AO and wireframe are shown as raw values.

## Offline renders and bakes

`raytrace.render` writes linear radiance to the `.hdr` file, plus a `.png` preview that uses the world's exposure, tonemapper and sRGB curve, so it looks the same as the viewport. Baked lightmaps and probes store linear values and go through the same tonemap pass as everything else.

## Keeping C++ and GLSL in sync

`rhi/display_transform.hpp` is a C++ copy of `shaders/common/tonemap.glsl` (same constants and formulas), used for the preview and the tests. If you change one, update the other.

## Things to keep in mind

- Scenes lit back when everything was clamped may look different now: shadows are brighter (no more crushed blacks) and highlights roll off instead of clipping. Adjust *Exposure* first, then light intensities.
- Custom shaders that write to the scene target must output linear values. Don't apply gamma in the shader.
- Color textures imported through a slot whose name doesn't start with *albedo*, *diffuse*, *emissive* or *basecolor* are treated as linear data, so name your slots accordingly.
