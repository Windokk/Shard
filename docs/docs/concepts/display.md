# Display: linear lighting, tonemapping and sRGB

Lighting is computed in linear space, but monitors expect sRGB and can't go brighter than white. The engine therefore renders in linear HDR and converts only at the very end, in two places:

```
sRGB textures --(decoded by the GPU when sampled)--> linear HDR scene --(tonemap pass)--> 8-bit sRGB image
```

## Pipeline

| Stage                                                    | Details                                                                                              |
|----------------------------------------------------------|------------------------------------------------------------------------------------------------------|
| Color textures (*albedo*, *diffuse*, *emissive*, *basecolor* slots) | Stored as sRGB (`GL_SRGB8_ALPHA8`, BC7 sRGB). The GPU converts them to linear when sampling and filters in linear space. Data textures (normal, metallic, roughness, AO) stay linear. The slot type comes from the sampler name (`IsColorTextureSlot`). Mipmaps of color textures are built in linear space, so a distant checkerboard averages to the right grey. |
| Sky | Radiance `.hdr` files are loaded as `RGB16F` floats, which keeps the sun far brighter than the sky. 8-bit skies and 6-face cubemaps are decoded from sRGB. The irradiance and prefilter convolutions clamp samples to 500, otherwise the sun disc overflows 16-bit floats and the diffuse term flickers. |
| Scene | The forward pass renders into a multisampled `RGBA16F` target, so values above 1 are kept. |
| Tonemap (`shaders/fb/tonemap.frag`) | Resolves the scene, applies exposure, the tonemapper and the sRGB curve, adds a little dithering against banding in skies and soft shadows, and writes the 8-bit viewport. |
| Overlays | Grid, outlines, billboards and debug shapes are drawn after tonemapping with their colors unchanged. They depth-test against a copy of the scene depth. |

`Renderer::GetViewportTextureHandle` still returns the final image, used by the editor, `--screenshot` and the player.

Thumbnails and material previews render straight to an 8-bit target, so `lit.frag` converts the color itself in that case (`outputDisplayColors`, ACES, no exposure).

## Settings

Found in *World Settings > Display*. They are written under `display` in the world file when they differ from the defaults.

| Setting | |
|---|---|
| Tonemapper | `aces` (default, filmic), `neutral` (Khronos PBR Neutral: leaves colors below ~0.8 alone and only compresses highlights), `reinhard`, or `none` (clamps, so anything above white is white). |
| Exposure (EV) | Applied before the curve. +1 doubles the light, -1 halves it. |

Debug views treat this differently. *Lit* goes through the full transform. *Unlit* and *No lighting* show albedo and only get the sRGB curve. Normals, depth, UVs, AO and wireframe show raw values.

## Offline renders and bakes

`raytrace.render` writes linear radiance to an `.hdr` file, and a `.png` preview that uses the world's exposure, tonemapper and sRGB curve so it matches the viewport. Baked lightmaps and probes store linear values and go through the same tonemap pass as everything else.

## Keeping C++ and GLSL in sync

`rhi/display_transform.hpp` is a C++ copy of `shaders/common/tonemap.glsl`, with the same constants and formulas. It is used for the preview and the tests. Change one, change the other.

## Notes

- A scene that was lit when everything was clamped will look different. Shadows are brighter (no crushed blacks) and highlights roll off instead of clipping. Adjust *Exposure* first, then light intensities.
- Custom shaders that write to the scene target must output linear values, with no gamma applied.
- A color texture only counts as color if its slot name starts with *albedo*, *diffuse*, *emissive* or *basecolor*. Any other name is treated as linear data.
