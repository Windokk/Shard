#version 430 core

// Fixed 4x4 box blur over the raw AO texture (ssao.frag) - matches the 4x4 tiled noise-rotation texture
// exactly, which is what actually needs removing (see SSAOManager::BuildKernelAndNoise); a wider/softer
// blur would also smear real occlusion edges without helping much.
//
// The blur is depth aware : a texel only counts as much as it lies on the same surface as the centre one.
// A plain box blur mixes the strongly occluded texels of a far surface (the red curtain tucked behind the
// edge of the blue one) into the pixels of the near surface's silhouette, which drew a dark outline along
// every depth discontinuity.

in vec2 texCoords;
out float FragAO;

uniform sampler2D ssaoInput;
uniform sampler2D gDepth;
uniform mat4 projection;

// Positive view-space distance of a hardware depth value (standard [0,1] range), from the projection's
// own terms : the same convention ssao.frag reconstructs positions with.
float LinearDepth(float depth)
{
    return projection[3][2] / ((depth * 2.0 - 1.0) + projection[2][2]);
}

void main()
{
    vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));

    float centerDepth = texture(gDepth, texCoords).r;
    float centerZ = LinearDepth(centerDepth);
    // How far another texel may sit from the centre one and still be the same surface : a few centimetres
    // up close, growing with distance as the depth buffer loses precision and the pixels cover more world.
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

    // The centre texel is one of the 16 and weighs 1, so the sum never vanishes
    FragAO = result / weightSum;
}
