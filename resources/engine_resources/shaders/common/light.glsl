// Must match Shard::Engine::Rendering::LightData exactly (std430 layout shared by every SSBO that reads it).
struct Light
{
    vec4 position;
    vec4 direction;
    vec4 color;
    float intensity;
    float radius;
    float innerCutoff;
    float outerCutoff;
    int type;
    int castShadow;
    int shadowIndex;
    int padding;

    // Emitter size for soft shadows in the offline renderers. Point/spot: sphere radius in world units. Directional: sun disc angular radius in degrees. 0 = hard shadows.
    float sourceSize;

    int bakeMode;
    int padding2;
    int padding3;
};

const int LIGHT_BAKE_DYNAMIC    = 0;
const int LIGHT_BAKE_STATIONARY = 1;
const int LIGHT_BAKE_STATIC     = 2;

const int LIGHTING_TERM_DIRECT            = 1;
const int LIGHTING_TERM_INDIRECT_DIFFUSE  = 2;
const int LIGHTING_TERM_INDIRECT_SPECULAR = 4;
const int LIGHTING_TERM_EMISSION          = 8;

// Smooth window on the inverse-square law, dying at `radius`. Must match lit.frag or offline renders get brighter far from lights. radius <= 0 means no window.
float LightRangeFalloff(float radius, float dist)
{
    if (radius <= 0.0)
        return 1.0;

    float f = clamp(1.0 - dist / radius, 0.0, 1.0);
    return f * f;
}
