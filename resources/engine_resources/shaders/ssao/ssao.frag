#version 430 core

// Classic hemisphere-kernel SSAO (Crytek/LearnOpenGL-style), sampling the depth+normal prepass
// (depth_normal.vert/.frag) instead of a G-buffer the engine doesn't otherwise have - see SSAOManager.
// Outputs one raw, single-channel occlusion value per pixel (1.0 = fully open, 0.0 = fully occluded) ;
// ssao_blur.frag removes the dither pattern the noise-texture rotation below introduces before this
// reaches lit.frag.
//
// Samplers/uniforms here are plain (no explicit layout(binding=...)) because this pass runs through
// RenderPass::overridePipeline - GLRendererAPI::BindPassData assigns texture units to `customSamplers`
// dynamically via glShader->SetInt(name, unit), not fixed layout qualifiers (see gl_api.cpp).

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

// Reconstructs a view-space position from a screen UV + hardware depth (standard [0,1] depth range),
// via the inverse of the same projection matrix used to draw the prepass - avoids needing a second
// "linear depth" attachment just to carry the same information a different way.
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
        // Sky / nothing rendered here (far plane) - never occluded, and there's no valid normal to
        // build a kernel orientation from anyway.
        FragAO = 1.0;
        return;
    }

    vec3 fragPos = ReconstructViewPos(texCoords, depth);
    vec3 normal = normalize(texture(gNormal, texCoords).rgb);

    // Tiled 4x4 random-rotation texture (see SSAOManager::BuildKernelAndNoise) - rotates the same fixed
    // kernel differently per pixel so the (otherwise banded) sampling pattern becomes noise instead,
    // which the blur pass then removes without needing many more samples.
    vec3 randomVec = normalize(texture(noiseTex, texCoords * noiseScale).xyz);

    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;

    for (int i = 0; i < KERNEL_SIZE; i++)
    {
        vec3 samplePos = fragPos + (TBN * samples[i]) * radius;

        vec4 offset = projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        float sampleDepthNDC = texture(gDepth, offset.xy).r;
        vec3 sampledViewPos = ReconstructViewPos(offset.xy, sampleDepthNDC);

        // View space looks down -Z, so a larger (less negative) Z is closer to the camera. If the real
        // surface at this screen position is closer than the kernel sample point, something occludes
        // it. rangeCheck fades the contribution out once the two are farther apart than `radius` -
        // otherwise a nearby tall occluder would darken surfaces far behind it that it can't actually
        // shadow the ambient term of.
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(fragPos.z - sampledViewPos.z), 1e-4));
        occlusion += (sampledViewPos.z >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = clamp(1.0 - (occlusion / float(KERNEL_SIZE)), 0.0, 1.0);

    // A flat linear average of boolean occlusion tests clusters close to mid-gray and never
    // reads as convincingly dark even in tight corners - this contrast curve (power > 1 darkens
    // occluded areas more aggressively) is what makes the effect actually visible; power = 1.0
    // reproduces the old flat/linear behavior.
    FragAO = pow(ao, power);
}
