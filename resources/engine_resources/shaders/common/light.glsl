// Mirrors Shard::Engine::Rendering::LightData exactly (see light_manager.hpp) - shared by every stage
// that reads the per-frame light list uploaded by LightManager : the offline path tracer
// (compute/path_trace.comp), the lightmap baker (compute/lightmap_bake.comp), the DDGI probe tracer
// (compute/probes/probe_trace.comp), and the forward pass (mesh/lit.frag). Field layout must stay in sync
// with the C++ struct - reordering or resizing this changes the std430 layout every SSBO reading it
// depends on.
struct Light
{
    vec4 position;
    vec4 direction;
    vec4 color;
    float intensity;
    float radius;
    float innerCutoff;
    float outerCutoff;
    int type; // 0 = Directional, 1 = Point, 2 = Spot
    int castShadow;
    int shadowIndex; // persistent slot into the shadow-map arrays/cube array layer, -1 if none assigned
    int padding;

    // Size of the emitter, for the soft shadows of the offline renderers (the path tracer and the lightmap
    // baker - the forward pass filters its shadow maps with a fixed kernel and ignores it). Point / spot :
    // radius of the emitting sphere in world units. Directional : angular radius of the sun disc in degrees.
    // 0 is an ideal point / direction : hard shadows.
    float sourceSize;

    // What the light does for baked lighting, see LIGHT_BAKE_* below
    int bakeMode;
    int padding2;
    int padding3;
};

// LightBakeMode (light_manager.hpp)
const int LIGHT_BAKE_DYNAMIC    = 0; // lit in real time only : never part of a lightmap
const int LIGHT_BAKE_STATIONARY = 1; // lit in real time ; its bounced (indirect) light is also baked into the lightmaps
const int LIGHT_BAKE_STATIC     = 2; // fully baked (direct + indirect) for the surfaces that have a lightmap ; still lit in real time for the others

// The terms the light leaving a surface is the sum of, as bits (LightingTerm in lighting_terms.hpp - keep in sync) : the forward pass
// can leave some out (render.lighting_terms) and the path tracer can write each apart, so each one can be compared against the other
const int LIGHTING_TERM_DIRECT            = 1; // the lights, straight onto the surface the camera sees
const int LIGHTING_TERM_INDIRECT_DIFFUSE  = 2; // all else that arrives through the diffuse lobe : sky, bounces, emitters, ambient floor
const int LIGHTING_TERM_INDIRECT_SPECULAR = 4; // all else that arrives through the specular lobe : reflections of the sky and the scene
const int LIGHTING_TERM_EMISSION          = 8; // the surface's own emission, and the sky where no surface is seen

// The window the forward pass puts on a point / spot light's inverse-square law : the light dies out smoothly at
// `radius` instead of reaching to infinity (lit.frag does the same inline). The offline renderers have to apply the
// very same window, or their picture is brighter far from every light than the real-time one. A radius of 0 (or
// less) means no window.
float LightRangeFalloff(float radius, float dist)
{
    if (radius <= 0.0)
        return 1.0;

    float f = clamp(1.0 - dist / radius, 0.0, 1.0);
    return f * f;
}
