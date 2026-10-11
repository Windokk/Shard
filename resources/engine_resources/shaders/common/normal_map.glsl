// A tangent-space normal map texel -> the normal it stores. Only x and y are read : the engine block compresses normal
// maps as two channels (BC5, see texture_compression.hpp), whose sampled blue channel is 0, and z is rebuilt from the
// unit length. A normal map kept uncompressed (a small one, a texture compression turned off) decodes the same, since its
// vectors are unit length and point out of the surface.
vec3 DecodeTangentNormal(vec4 texel)
{
    vec2 xy = texel.xy * 2.0 - 1.0;
    return vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
}
