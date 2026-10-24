#version 430 core
#extension GL_ARB_bindless_texture : require

in vec2 vUV;
in vec4 vColor;
in float vCustom;
flat in uvec2 vTexture;
flat in uint vFlags;

out vec4 FragColor;

void main()
{
    vec4 color = vColor;
    if (vTexture != uvec2(0u))
        color *= texture(sampler2D(vTexture), vUV);
    // Flag 4 : opaque (the alpha is ignored)
    if ((vFlags & 4u) != 0u)
        color.a = 1.0;
    // Flag 2 : cut out (alpha below one half is dropped)
    if ((vFlags & 2u) != 0u && color.a < 0.5)
        discard;
    if (color.a < 0.003)
        discard;
    FragColor = color;
}
