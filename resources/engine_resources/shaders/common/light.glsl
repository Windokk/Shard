// Mirrors Shard::Engine::Rendering::LightData exactly (see light_manager.hpp) - shared by every stage
// that reads the per-frame light list uploaded by LightManager : the offline path tracer
// (compute/path_trace.comp), the DDGI probe tracer (compute/probes/probe_trace.comp), and the forward
// pass (mesh/lit.frag). Field layout must stay in sync with the C++ struct - reordering or resizing
// this changes the std430 layout every SSBO reading it depends on.
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
};
