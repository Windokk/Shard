#version 430 core

// The last pass of the scene : the HDR image the forward pass rendered (linear light, any range) becomes the 8 bit picture the
// viewport, the screenshots and the window show (see display_transform.hpp). The editor's overlays (grid, outlines, icons, debug
// shapes) are drawn on that result afterwards, in the colours they are authored in.

in vec2 texCoords;
out vec4 fragColor;

#include "../common/tonemap.glsl"

layout(binding = 0) uniform sampler2D sceneTexture;         // the resolved HDR color of the forward pass
layout(binding = 1) uniform sampler2DMS sceneTextureMS;     // the same, before the resolve : its samples

// How many samples sceneTextureMS holds, 0 when the scene was not multisampled (sceneTexture is then the image itself).
// The tonemapper is not linear, so it must not see the average of the samples of an edge pixel : the red of a curtain mixed
// with the blue in front of it has a larger darkest channel than either, which the Neutral curve takes off every channel,
// and the pixel came out as a black outline. Each sample is tonemapped on its own, and it is the display colours that are
// averaged.
uniform int sceneSamples;

uniform float exposure;             // a linear factor : 2^EV
uniform int tonemapper;             // see TONEMAP_*

// What the picture is made of, which decides how it is shown (Renderer::GetDisplayMode) :
//   0  light               : exposure, tonemapper, sRGB curve
//   1  linear colours      : clamped, sRGB curve only (a view of the albedo : no light to roll off)
//   2  display colours     : untouched (debug views whose colours were authored to be shown as they are : normals, depth, UVs, AO...)
uniform int displayMode;

// One sample, taken to the linear [0, 1] light that is then averaged (the sRGB curve comes after, on the average)
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
        // Already display colours : not a bit is changed (not even a dither)
        fragColor = vec4(linear, 1.0);
        return;
    }

    fragColor = vec4(DitherForDisplay(LinearToSRGB(linear), gl_FragCoord.xy), 1.0);
}
