#version 430 core

struct Instance {
    vec4 positionSize;
    vec4 tint;
    vec4 uv;
    uvec4 params;
};

layout(std430, binding = 24) readonly buffer BillboardInstances {
    Instance instances[];
};

uniform mat4 uProjection;
uniform mat4 uView;

uniform vec2 viewportSize;
uniform int firstInstance;

out vec2 vUV;
out vec4 vTint;

const vec2 kCorners[6] = vec2[](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0)
);

void main()
{
    Instance inst = instances[firstInstance + gl_InstanceID];
    vec2 corner = kCorners[gl_VertexID];

    vec3 rel = inst.positionSize.xyz;
    float size = inst.positionSize.w;
    uint facing = inst.params.x;
    bool pixelSize = (inst.params.y & 1u) != 0u;

    mat3 rotation = mat3(uView);
    vec3 center = rotation * rel;

    vec4 centerClip = uProjection * vec4(center, 1.0);
    if (centerClip.w <= 0.0)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vUV = vec2(0.0);
        vTint = vec4(0.0);
        return;
    }

    float half_ = pixelSize ? size * centerClip.w / (viewportSize.y * uProjection[1][1])
                            : size * 0.5;

    vec3 viewPos;
    if (facing == 1u)
    {
        vec3 up = normalize(rotation * vec3(0.0, 1.0, 0.0));
        vec3 toCamera = normalize(-center);
        vec3 right = cross(up, toCamera);
        right = length(right) > 1e-4 ? normalize(right) : vec3(1.0, 0.0, 0.0);
        viewPos = center + (right * corner.x + up * corner.y) * half_;
    }
    else if (facing == 2u)
    {
        viewPos = rotation * (rel + vec3(corner * half_, 0.0));
    }
    else
    {
        viewPos = center + vec3(corner * half_, 0.0);
    }

    gl_Position = uProjection * vec4(viewPos, 1.0);
    vUV = vec2(mix(inst.uv.x, inst.uv.z, corner.x * 0.5 + 0.5), mix(inst.uv.y, inst.uv.w, corner.y * 0.5 + 0.5));
    vTint = inst.tint;
}
