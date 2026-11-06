// Reads only x and y (BC5 normal maps have blue = 0), z is rebuilt from unit length.
vec3 DecodeTangentNormal(vec4 texel)
{
    vec2 xy = texel.xy * 2.0 - 1.0;
    return vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
}
