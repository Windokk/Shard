#version 430 core
#extension GL_ARB_bindless_texture : require

in vec4 vColor;
in vec2 vUV;

uniform uvec2 uTexture;         // bindless handle, 0 : none

out vec4 FragColor;

void main()
{
    vec4 color = vColor;
    if (uTexture != uvec2(0u))
        color *= texture(sampler2D(uTexture), vUV);
    FragColor = color;
}
