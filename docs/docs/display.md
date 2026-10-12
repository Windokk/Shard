# Display : linear light, tonemapping and sRGB

The renderer computes **light**, and light adds and multiplies only in linear units. A monitor, on the other hand, expects colours in the sRGB transfer curve, and shows nothing brighter than white. The engine therefore works in two spaces and converts between them in exactly two places :

```
sRGB textures ──(decoded by the GPU on sampling)──▶  linear HDR scene  ──(tonemap pass)──▶  8 bit sRGB picture
```

## What is where

| Stage | What happens |
|---|---|
| **Colour textures** (*albedo, diffuse, emissive, basecolor* slots) | Stored as sRGB (`GL_SRGB8_ALPHA8`, BC7 sRGB) : the hardware turns them into linear values when the shader samples them, and filters in linear light. Data textures (normal, metallic, roughness, AO) stay linear. The slot is chosen by the sampler's name (`IsColorTextureSlot`). Mip levels of a colour texture are averaged in linear light, so a distant checkerboard is the right grey instead of a murky dark one. |
| **Sky** | A Radiance `.hdr` is loaded as floats (`RGB16F`) : a sun keeps being many times brighter than the sky. 8 bit skies and 6 face cubemaps are decoded from sRGB. The environment convolutions (irradiance, prefilter) clamp their samples to 500 so a sun disc cannot overflow the 16 bit format or flicker the diffuse term. |
| **Scene** | The forward pass draws into an `RGBA16F` multisampled target : lighting values go far above 1 without being clipped. |
| **Tonemap pass** (`shaders/fb/tonemap.frag`) | Resolves the scene, multiplies it by the exposure, applies the tonemapper, then the sRGB curve (and half a level of dither, against banding in skies and soft shadows), and writes the 8 bit viewport. |
| **Overlays** | The grid, outlines, billboards and debug shapes are drawn on the viewport after the tonemap, in the colours they are authored in. The depth they are tested against is copied from the scene. |

The viewport (`Renderer::GetViewportTextureHandle`) is still the *picture* : the editor UI, `--screenshot` and the player present it as before.

The studio views of thumbnails and material previews render straight to an 8 bit target : `lit.frag` applies the same transform itself there (`outputDisplayColors`, ACES, no exposure).

## Settings

*World Settings → Display*, stored in the world file under `display` (only when it differs from the default) :

| Setting | |
|---|---|
| Tonemapper | `aces` (default, the filmic Academy curve), `neutral` (Khronos PBR Neutral : colours below ~0.8 stay what they are, only highlights roll off), `reinhard`, `none` (clamp : everything brighter than white is white). |
| Exposure (EV) | Stops added before the curve : +1 doubles the light, -1 halves it. |

Debug views choose how the picture is mapped : *Lit* goes through the whole transform ; *Unlit* and *No lighting* are albedo views, so only the sRGB curve is applied ; normals, depth, UVs, AO and wireframe are colours meant to be seen as they are and pass through untouched.

## Offline render and bakes

`raytrace.render` writes the **linear** radiance to its `.hdr` and a `.png` preview that goes through the world's exposure, tonemapper and sRGB curve : what the preview shows is what the viewport shows. Baked lightmaps and probes store linear light and are shown through the same pass as everything else.

## Keeping the C++ and the shader in step

`rhi/display_transform.hpp` is the C++ twin of `shaders/common/tonemap.glsl` (same constants, same formulas), used by the preview and the tests. Change one, change the other.

## Authoring consequences

* A light that looked right when everything was clamped may now look different : the dark parts are brighter (no more crushed shadows), the highlights roll off instead of clipping. Use *Exposure* first, then the light intensities.
* A custom shader that outputs to the scene target outputs **linear** light ; it must not apply a gamma itself.
* Colour textures imported through a slot whose name does not start with *albedo / diffuse / emissive / basecolor* are read as linear data : name the slot accordingly.
