#version 430 core

in vec2 texCoords;
out vec4 FragColor;

// Explicit binding required - see the comment in jfa_step.frag.
layout(binding = 0) uniform sampler2D maskTex;

// Jump Flood Algorithm - seed pass. Every pixel that belongs to the selected object's silhouette
// (see outline_mask.frag) becomes its own seed, storing its pixel-space position so later passes can
// propagate "nearest seed" information outward. FragColor.z doubles as a validity flag (1.0 = this
// pixel carries a real seed) rather than relying on a magic sentinel coordinate, which would risk a
// false match near the origin. Alpha is always 1.0 (not left at 0) so a write here can never be
// silently nullified if alpha blending ends up active on this pipeline for any reason.
void main()
{
    float mask = texture(maskTex, texCoords).r;

    if (mask > 0.5)
        FragColor = vec4(gl_FragCoord.xy, 1.0, 1.0);
    else
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
