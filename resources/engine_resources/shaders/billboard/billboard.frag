#version 430 core

in vec2 vUV;
in vec4 vTint;

out vec4 FragColor;

uniform sampler2D uAtlas;

void main()
{
    vec4 color = texture(uAtlas, vUV) * vTint;
    if (color.a < 0.02)
        discard;
    FragColor = color;
}
