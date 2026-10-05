#version 430 core

// Fixed 4x4 box blur over the raw AO texture (ssao.frag) - matches the 4x4 tiled noise-rotation texture
// exactly, which is what actually needs removing (see SSAOManager::BuildKernelAndNoise); a wider/softer
// blur would also smear real occlusion edges without helping much.

in vec2 texCoords;
out float FragAO;

uniform sampler2D ssaoInput;

void main()
{
    vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));

    float result = 0.0;
    for (int x = -2; x < 2; x++)
    {
        for (int y = -2; y < 2; y++)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += texture(ssaoInput, texCoords + offset).r;
        }
    }

    FragAO = result / 16.0;
}
