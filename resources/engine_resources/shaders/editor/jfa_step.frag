#version 430 core

in vec2 texCoords;
out vec4 FragColor;

layout(binding = 0) uniform sampler2D seedTex;
uniform float stepSize;
uniform vec2 texelSize;

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
