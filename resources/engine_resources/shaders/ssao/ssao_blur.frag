#version 430 core

// Depth-aware 4x4 box blur (matches the 4x4 noise tile). A plain box smears far-surface occlusion into near silhouettes as a dark outline.

in vec2 texCoords;
out float FragAO;

uniform sampler2D ssaoInput;
uniform sampler2D gDepth;
uniform mat4 projection;

float LinearDepth(float depth)
{
    return projection[3][2] / ((depth * 2.0 - 1.0) + projection[2][2]);
}

void main()
{
    vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));

    float centerDepth = texture(gDepth, texCoords).r;
    float centerZ = LinearDepth(centerDepth);
    float tolerance = max(0.05, 0.02 * abs(centerZ));

    float result = 0.0;
    float weightSum = 0.0;
    for (int x = -2; x < 2; x++)
    {
        for (int y = -2; y < 2; y++)
        {
            vec2 uv = texCoords + vec2(float(x), float(y)) * texelSize;

            float dz = (LinearDepth(texture(gDepth, uv).r) - centerZ) / tolerance;
            float w = exp(-dz * dz);

            result += texture(ssaoInput, uv).r * w;
            weightSum += w;
        }
    }

    FragAO = result / weightSum;
}
