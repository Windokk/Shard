// Mirrors LightCullingManager's C++ constexprs. They size the cluster SSBOs, so a mismatch reads/writes out of bounds.
#define CLUSTER_TILE_PX 64
#define CLUSTER_Z_SLICES 24
#define MAX_LIGHTS_PER_CLUSTER 128
