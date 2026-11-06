#version 430 core

in vec2 texCoords;
out vec4 fragColor;

#include "../common/tonemap.glsl"

layout(binding = 0) uniform sampler2D sceneTexture;
layout(binding = 1) uniform sampler2DMS sceneTextureMS;

// Sample count of sceneTextureMS, 0 if not multisampled. Tonemap each sample and average the display colours. Averaging HDR first made a black outline on bright edges with the Neutral curve.
uniform int sceneSamples;

uniform float exposure;
uniform int tonemapper;

uniform int displayMode;

vec3 ToDisplayLinear(vec3 scene)
{
    if (displayMode == 0)
        return ApplyTonemap(scene * exposure, tonemapper);
    return clamp(scene, 0.0, 1.0);
}

void main()
{
    vec3 linear;

    if (sceneSamples > 0)
    {
        ivec2 pixel = ivec2(gl_FragCoord.xy);
        vec3 sum = vec3(0.0);
        for (int s = 0; s < sceneSamples; s++)
            sum += ToDisplayLinear(texelFetch(sceneTextureMS, pixel, s).rgb);
        linear = sum / float(sceneSamples);
    }
    else
    {
        linear = ToDisplayLinear(texture(sceneTexture, texCoords).rgb);
    }

    if (displayMode == 2)
    {
        fragColor = vec4(linear, 1.0);
        return;
    }

    fragColor = vec4(DitherForDisplay(LinearToSRGB(linear), gl_FragCoord.xy), 1.0);
}
