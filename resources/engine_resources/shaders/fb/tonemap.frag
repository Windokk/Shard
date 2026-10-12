#version 430 core

// The last pass of the scene : the HDR image the forward pass rendered (linear light, any range) becomes the 8 bit picture the
// viewport, the screenshots and the window show (see display_transform.hpp). The editor's overlays (grid, outlines, icons, debug
// shapes) are drawn on that result afterwards, in the colours they are authored in.

in vec2 texCoords;
out vec4 fragColor;

#include "../common/tonemap.glsl"

uniform sampler2D sceneTexture;     // the resolved HDR color of the forward pass

uniform float exposure;             // a linear factor : 2^EV
uniform int tonemapper;             // see TONEMAP_*

// What the picture is made of, which decides how it is shown (Renderer::GetDisplayMode) :
//   0  light               : exposure, tonemapper, sRGB curve
//   1  linear colours      : clamped, sRGB curve only (a view of the albedo : no light to roll off)
//   2  display colours     : untouched (debug views whose colours were authored to be shown as they are : normals, depth, UVs, AO...)
uniform int displayMode;

void main()
{
    vec3 scene = texture(sceneTexture, texCoords).rgb;
    vec3 display;

    if (displayMode == 2)
    {
        // Already display colours : not a bit is changed (not even a dither)
        fragColor = vec4(clamp(scene, 0.0, 1.0), 1.0);
        return;
    }

    if (displayMode == 0)
        display = DisplayTransform(scene, tonemapper, exposure);
    else
        display = LinearToSRGB(scene);

    fragColor = vec4(DitherForDisplay(display, gl_FragCoord.xy), 1.0);
}
