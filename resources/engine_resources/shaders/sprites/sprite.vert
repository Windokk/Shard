#version 430 core

struct Sprite {
    vec4 positionRotation;
    vec4 sizeCustom;
    vec4 uv;
    vec4 axis;
    uvec4 data;
};

layout(std430, binding = 25) readonly buffer SpriteInstances {
    Sprite sprites[];
};

uniform mat4 uProjection;
uniform mat4 uView;
uniform vec2 viewportSize;
uniform int firstInstance;

out vec2 vUV;
out vec4 vColor;
out float vCustom;
flat out uvec2 vTexture;
flat out uint vFlags;

const vec2 kCorners[6] = vec2[](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0)
);

void main()
{
    Sprite s = sprites[firstInstance + gl_InstanceID];
    vec2 corner = kCorners[gl_VertexID];

    uint facing = s.data.w & 0xFFu;
    uint flags = s.data.w >> 8;
    bool pixelSize = (flags & 1u) != 0u;

    mat3 rotation = mat3(uView);
    vec3 center = rotation * s.positionRotation.xyz;

    vec4 centerClip = uProjection * vec4(center, 1.0);
    if (centerClip.w <= 0.0)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vUV = vec2(0.0); vColor = vec4(0.0); vCustom = 0.0; vTexture = uvec2(0u); vFlags = 0u;
        return;
    }

    vec2 size = s.sizeCustom.xy;
    if (pixelSize)
        size *= centerClip.w / (viewportSize.y * uProjection[1][1]) * 2.0;
    vec2 half_ = size * 0.5;

    float c = cos(s.positionRotation.w), sn = sin(s.positionRotation.w);
    vec2 rc = vec2(corner.x * c - corner.y * sn, corner.x * sn + corner.y * c);

    vec3 viewPos;
    if (facing == 1u)
    {
        vec3 up = rotation * s.axis.xyz;
        up = length(up) > 1e-5 ? normalize(up) : vec3(0.0, 1.0, 0.0);
        vec3 toCamera = normalize(-center);
        vec3 right = cross(up, toCamera);
        right = length(right) > 1e-4 ? normalize(right) : vec3(1.0, 0.0, 0.0);
        viewPos = center + right * (corner.x * half_.x) + up * (corner.y * half_.y);
    }
    else if (facing == 2u)
    {
        vec3 n = normalize(length(s.axis.xyz) > 1e-5 ? s.axis.xyz : vec3(0.0, 1.0, 0.0));
        vec3 t = normalize(cross(n, abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
        vec3 b = cross(n, t);
        viewPos = rotation * (s.positionRotation.xyz + (t * rc.x * half_.x + b * rc.y * half_.y));
    }
    else
    {
        viewPos = center + vec3(rc * half_, 0.0);
    }

    gl_Position = uProjection * vec4(viewPos, 1.0);
    vUV = vec2(mix(s.uv.x, s.uv.z, corner.x * 0.5 + 0.5), mix(s.uv.y, s.uv.w, corner.y * 0.5 + 0.5));
    vColor = vec4(float(s.data.x & 0xFFu), float((s.data.x >> 8) & 0xFFu), float((s.data.x >> 16) & 0xFFu), float(s.data.x >> 24)) / 255.0;
    vCustom = s.sizeCustom.z;
    vTexture = s.data.yz;
    vFlags = flags;
}
