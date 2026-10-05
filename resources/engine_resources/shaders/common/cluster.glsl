// Shared constants for clustered forward+ light culling (point/spot lights only - directional lights
// are few enough that lit.frag still scans them directly). Consumed by LightCullingManager's two
// compute shaders (cluster_build.comp, cluster_light_cull.comp) and by mesh/lit.frag. Keep these in
// sync with LightCullingManager's mirrored C++ constexprs - they size the ClusterAABB/LightGrid/
// LightIndexList SSBOs, so a mismatch between a shader rebuilt here and a buffer sized in C++ will
// read/write out of bounds.
#define CLUSTER_TILE_PX 64
#define CLUSTER_Z_SLICES 24
#define MAX_LIGHTS_PER_CLUSTER 128
