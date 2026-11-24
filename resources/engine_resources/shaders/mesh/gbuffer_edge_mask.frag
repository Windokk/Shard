#version 430 core

// Marks (1) the pixels of the multisampled G-buffer whose samples do not all show the same surface. lit.frag (deferred lighting) lights the other pixels once,
// from sample 0, and only the marked ones once per sample. Same texture units as lit.frag, so the lighting passes share the sampler names.
//
// What MSAA anti-aliases is geometry, so an edge here is a silhouette (the sky, or a depth jump) or a crease (the vertex normal jumps). The material and the normal map
// are not compared : two triangles of one surface sample them at slightly different places, and on a dense mesh that flagged almost half of Sponza for nothing.
//
// Only the G-buffer's normals and depth are read, but the sampler names and units are lit.frag's, so the lighting passes and this one share them.

layout(binding = 1) uniform sampler2DMS gNormalsTex;
layout(binding = 10) uniform sampler2DMS gDepthTex;

uniform mat4 uInvViewProjection;
uniform int uGBufferSamples;

out vec4 fragColor;

// Depth is the one thing that changes across a pixel of a plane, so it is compared in 1/z (affine in screen space) : the w of the unprojected point is 1 / view depth.
float InverseViewDepth(vec2 ndc, float depth)
{
    vec4 clip = vec4(ndc, depth * 2.0 - 1.0, 1.0);
    return uInvViewProjection[0][3] * clip.x + uInvViewProjection[1][3] * clip.y + uInvViewProjection[2][3] * clip.z + uInvViewProjection[3][3];
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec2 ndc = gl_FragCoord.xy / vec2(textureSize(gDepthTex)) * 2.0 - 1.0;

    float depth0 = texelFetch(gDepthTex, pixel, 0).r;
    bool sky0 = depth0 >= 1.0;
    float invZ0 = sky0 ? 0.0 : InverseViewDepth(ndc, depth0);

    // .zw : the octahedral vertex normal (.xy is the normal-mapped one).
    vec2 vertexNormal0 = sky0 ? vec2(0.0) : texelFetch(gNormalsTex, pixel, 0).zw;

    bool edge = false;
    for (int s = 1; s < uGBufferSamples && !edge; ++s)
    {
        float depth = texelFetch(gDepthTex, pixel, s).r;
        bool sky = depth >= 1.0;
        if (sky != sky0)
        {
            edge = true;
            break;
        }
        if (sky)
            continue;

        // 2 % of the depth : a plane seen at 87 degrees changes by less than that across half a pixel at 720p.
        if (abs(InverseViewDepth(ndc, depth) - invZ0) > 0.02 * abs(invZ0))
            edge = true;
        // 0.15 in octahedral units is about 10 degrees : smooth surfaces stay below it, a crease does not.
        else if (any(greaterThan(abs(texelFetch(gNormalsTex, pixel, s).zw - vertexNormal0), vec2(0.15))))
            edge = true;
    }

    fragColor = vec4(edge ? 1.0 : 0.0, 0.0, 0.0, 1.0);
}
