#version 430 core

in vec2 texCoords;
out vec4 FragColor;

// Explicit binding required - this engine assigns texture units via GLSL layout(binding=N) at
// compile time (see lit.frag), not via runtime glUniform1i (BindPassData's customSamplers loop only
// tracks plain uniforms, not samplers - an un-annotated sampler silently keeps GLSL's default binding
// of 0 no matter which unit the texture is actually bound to).
layout(binding = 0) uniform sampler2D seedTex;
uniform float stepSize;
uniform vec2 texelSize;

// Jump Flood Algorithm - propagation step. For each of the 8 neighbors offset by stepSize pixels
// (halved every pass by the caller - see the EditorJFAStep* chain in main_window.cpp), keep whichever
// seed (this pixel's current one included) is closest to this pixel's own position. Run over a fixed
// falling step sequence (16,8,4,2,1) this converges exactly for any pixel within ~31px of a seed,
// which comfortably covers a selection outline's thickness without needing the full
// log2(max(width,height)) chain a whole-image distance transform would require.
void main()
{
    vec2 myPixel = gl_FragCoord.xy;

    vec4 best = texture(seedTex, texCoords);
    float bestDist = (best.z > 0.5) ? distance(myPixel, best.xy) : 1e20;

    for (int dy = -1; dy <= 1; dy++)
    {
        for (int dx = -1; dx <= 1; dx++)
        {
            if (dx == 0 && dy == 0)
                continue;

            vec2 sampleUV = texCoords + vec2(dx, dy) * stepSize * texelSize;

            if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
                continue;

            vec4 s = texture(seedTex, sampleUV);

            if (s.z > 0.5)
            {
                float d = distance(myPixel, s.xy);
                if (d < bestDist)
                {
                    bestDist = d;
                    best = s;
                }
            }
        }
    }

    FragColor = best;
}
