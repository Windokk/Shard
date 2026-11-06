#version 430 core

in vec2 texCoords;
out vec4 FragColor;

layout(binding = 0) uniform sampler2D maskTex;

// Jump Flood seed pass. z is the validity flag (1 = real seed), no magic sentinel coordinate. Alpha is 1.0 so alpha blending cannot nullify the write.
void main()
{
    float mask = texture(maskTex, texCoords).r;

    if (mask > 0.5)
        FragColor = vec4(gl_FragCoord.xy, 1.0, 1.0);
    else
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
