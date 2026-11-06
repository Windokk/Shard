#version 430 core

// Hemisphere-kernel SSAO. Samplers have no layout(binding): this runs through RenderPass::overridePipeline and BindPassData assigns units to customSamplers at run time.

in vec2 texCoords;
out float FragAO;

uniform sampler2D gDepth;
uniform sampler2D gNormal;
uniform sampler2D noiseTex;

uniform mat4 projection;
uniform vec2 noiseScale;
uniform float radius;
uniform float bias;
uniform float power;

const int KERNEL_SIZE = 32;
uniform vec3 samples[KERNEL_SIZE];

vec3 ReconstructViewPos(vec2 uv, float depth)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = inverse(projection) * clip;
    return view.xyz / view.w;
}

void main()
{
    float depth = texture(gDepth, texCoords).r;
    if (depth >= 1.0)
    {
        FragAO = 1.0;
        return;
    }

    vec3 fragPos = ReconstructViewPos(texCoords, depth);
    vec3 normal = normalize(texture(gNormal, texCoords).rgb);

    vec3 randomVec = normalize(texture(noiseTex, texCoords * noiseScale).xyz);

    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    // Depth is read at a texel centre, so on grazing surfaces rounding alone put the floor in front of the sample (dark bands). Bias grows with the depth change per pixel, capped so silhouettes still occlude.
    float texelDepthStep = abs(dFdx(fragPos.z)) + abs(dFdy(fragPos.z));
    float sampleBias = bias + min(texelDepthStep, 0.1 * abs(fragPos.z));

    float occlusion = 0.0;

    for (int i = 0; i < KERNEL_SIZE; i++)
    {
        vec3 samplePos = fragPos + (TBN * samples[i]) * radius;

        vec4 offset = projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        float sampleDepthNDC = texture(gDepth, offset.xy).r;
        vec3 sampledViewPos = ReconstructViewPos(offset.xy, sampleDepthNDC);

        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(fragPos.z - sampledViewPos.z), 1e-4));
        occlusion += (sampledViewPos.z >= samplePos.z + sampleBias ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = clamp(1.0 - (occlusion / float(KERNEL_SIZE)), 0.0, 1.0);

    FragAO = pow(ao, power);
}
