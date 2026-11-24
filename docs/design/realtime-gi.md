# Switchable realtime GI

Personal notes, nothing implemented yet.

## Goal

Pick a world's GI model in the editor, switchable at runtime: baked lightmaps, SSGI, DDGI, radiance cascades, others later (VCT, SDFGI, ReSTIR GI). One model = one module behind a common interface, the surface shader only sees the result.

Out of scope: direct lighting, shadows, the lightmap baker, the offline path tracer (it is the reference).

## Existing

- Lightmaps: `LightmapManager`, bindless page in `lit.frag`.
- DDGI: `ProbeManager`, `ProbeVolume`, bindless atlases, SSBO 14+, 2 volumes max (`kMaxProbeVolumes`).
- SSAO (`SSAOManager`) and SSR (`SSRManager`: Hi-Z, previous frame color pyramid, hierarchical trace, temporal).
- Persistent BVH (`features/raytracing`), already used by DDGI.
- forward+ and deferred pipelines, render graph with `graphAccesses`.
- World settings in `RenderWorldData` (`ssr`, `display`, `lightmapSettings`) + World Settings panel.

Problem: the choice is hardwired in `ShadeSurface` (DDGI if a volume is ready, else lightmap, else IBL), and the managers are members of `Renderer`. Every new model would add more `if`s.

## Architecture

```
settings.gi.model = "ddgi"
   GIRegistry -> GIProvider (one active per view)
        BuildPasses()  compute passes in the render graph
        Bind()         bindless handles + GIParams buffer
        ShaderDefine() SHARD_GI_MODEL_xxx
   lit.frag / deferred lighting : GI_Evaluate(GISurface)
```

### Interface (`renderer/features/gi/`)

```cpp
struct GICapabilities {
    bool needsDepthNormals, needsColorHistory, needsRaytraceScene;
    bool usesBakedData, providesOcclusion, providesSpecular;
};

class GIProvider {
public:
    virtual const char* GetId() const = 0;   // "baked", "ssgi", "ddgi", "rc"
    virtual GICapabilities GetCapabilities() const = 0;
    virtual bool Supports(const RendererCaps&, const RenderPipeline&) const;
    virtual void Activate(Renderer&);
    virtual void Deactivate(Renderer&);      // frees VRAM and histories
    virtual void BeforeFrame(Renderer&, const RenderSnapshot&);
    virtual void BuildPasses(Renderer&, GIPassBuilder&);
    virtual void Bind(Renderer&, Shader&);
    virtual const char* GetShaderDefine() const;
    virtual nlohmann::json GetSettings() const;
    virtual void SetSettings(const nlohmann::json&);
    virtual std::vector<GISettingDesc> DescribeSettings() const;  // the editor generates the UI
    virtual GIStats GetStats() const;        // GPU ms, VRAM
};
```

`GIRegistry` copies `RenderPipelineRegistry`. Baked and DDGI wrap `LightmapManager` and `ProbeManager`, no rewrite.

### Shader contract (`shaders/common/gi.glsl`)

```glsl
struct GISurface { vec3 P, N, V, albedo; float roughness, metallic; vec2 screenUV, lightmapUV; bool hasLightmap; };
struct GIResult  { vec3 diffuse; vec3 specular; float occlusion; float confidence; };
GIResult GI_Evaluate(GISurface s);
```

- `confidence` < 1 when a screen-space model has no data (off screen, invalid history): blend with the fallback (IBL, or DDGI).
- The GI block of `ShadeSurface` is replaced by this call. The rest stays: reflection probes, SSR, ambient, SSAO (unless `providesOcclusion`), excluded terms.
- Selection by permutation (`Shader::GetVariant`, `ShardShaderc --define`): resources differ from one model to another. Async recompile, the old variant stays until the new one is ready.
- `lit.frag` already uses 11 samplers: all GI data goes bindless through `GIParams`.

### Settings and editor

- `RenderWorldData::gi { model, settings }`, serialized in `settings.gi` only when different from the default.
- Default: `auto`, the current behavior (lightmap on statics, DDGI if a volume exists, else IBL). Existing worlds don't change.
- World Settings > Rendering > Global Illumination: model combo (greyed out when `Supports` is false), generated settings, Bake/Clear, GPU ms and VRAM.
- Console: `gi.model <id>`, `gi.status`, `gi.settings <json>`. Debug views: GI only, plus one per model (probes, cascades, confidence).
- Switching model: `Deactivate` the old one, `Activate` the new one, histories invalidated.

## Models

| Model | Principle | Limits |
|---|---|---|
| Off | IBL + ambient | no GI |
| Baked | lightmaps on statics, baked probes for dynamics | frozen |
| SSGI | one cosine ray per pixel at half resolution in the Hi-Z, reads the previous color, temporal accumulation + a-trous | screen only, fallback off screen |
| DDGI | current `ProbeManager` | 2 volumes, light leaks |
| Radiance cascades | cascade n spaced 2^n, 4^n directions, growing ray interval, top-down merge. v1 screen (depth + color), v2 world on the BVH | v1 screen only, merge is tricky (visibility, edges) |

One main model per view. Stacking (SSGI over DDGI) comes last.

## Phases

0. Refactor with no visual change: interface, registry, `gi.glsl`, Off/Baked/DDGI providers, `auto` mode, settings, panel, console. Done when: Sponza and `lightmaps.world` match the previous output (mean diff < 1/255), forward+ and deferred, GL and Vulkan, existing tests green.
1. Move the Hi-Z and color pyramid out of `SSRManager` into a shared service (SSR, SSGI, RC). Done when: SSR unchanged.
2. SSGI: half-res trace, temporal accumulation, a-trous (see `lightmap_atrous.comp`), Low to Ultra qualities like SSR.
3. Radiance cascades: v1 screen, then v2 world if v1 holds up.
4. Model stacking, presets, GPU budget, plugin extension (ABI).

## Watch out

- Samplers: bindless is mandatory.
- Permutations: model x forward/deferred x MSAA. Precompile with shaderc.
- MSAA: screen-space models run at half resolution, bilateral upsample (depth + normal).
- Double counting: Static lights already in the lightmap, SSAO already applied if `providesOcclusion`, `ambientColor` once.
- Vulkan: DDGI and lightmaps not verified. New models pass `parity_test.sh` from their phase on.
- Headless: GI must not touch the simulation or the state hash, providers inactive on the null RHI.

## Validation

- Unit tests: registry, `gi` serialization (missing = `auto`), model switch without leaks.
- Each model compared to the path tracer's indirect diffuse term (`raytrace.render ... terms`) on the Cornell box and Sponza.
- GL/VK and forward+/deferred parity, clean Vulkan validation.
- GPU ms and VRAM measured on Sponza.
