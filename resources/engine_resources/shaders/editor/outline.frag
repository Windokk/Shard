#version 430 core

in vec2 texCoords;
out vec4 FragColor;

// Explicit bindings required - this engine assigns texture units via GLSL layout(binding=N) at
// compile time (see lit.frag), not via runtime glUniform1i (BindPassData's customSamplers loop only
// tracks plain uniforms, not samplers - an un-annotated sampler silently keeps GLSL's default binding
// of 0 no matter which unit the texture is actually bound to). The indices below must match the
// alphabetical order BindPassData assigns slots in (it iterates pass->customSamplers, a
// std::map<std::string, ...>): "maskTex" < "seedTex", so maskTex=0, seedTex=1.
layout(binding = 0) uniform sampler2D maskTex;
layout(binding = 1) uniform sampler2D seedTex;
uniform vec3 outlineColor;
uniform float outlineThickness;

// Jump Flood Algorithm - resolve pass. seedTex (after EditorJFAStep0..4 - see main_window.cpp) holds,
// for every pixel, the position of its nearest silhouette pixel; converting that into a true Euclidean
// distance gives a uniformly thick outline in every direction, unlike the old approach of comparing a
// fixed 3x3 ring of neighbor mask samples (which only ever caught 1px-aligned edges and produced an
// outline whose thickness varied with edge angle).
void main()
{
    float mask = texture(maskTex, texCoords).r;

    // Never draw over the selected object itself - only in the halo around it.
    if (mask > 0.5)
        discard;

    vec4 seed = texture(seedTex, texCoords);

    // No seed reached this pixel (it's farther from the silhouette than the JFA step chain covers) -
    // definitely outside outlineThickness, so nothing to draw here.
    if (seed.z < 0.5)
        discard;

    float dist = distance(gl_FragCoord.xy, seed.xy);

    if (dist > outlineThickness)
        discard;

    FragColor = vec4(outlineColor, 1.0);
}
