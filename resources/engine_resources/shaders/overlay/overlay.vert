#version 430 core

struct OverlayVertex {
    float x, y;
    uint color;
    float u, v;
};

layout(std430, binding = 26) readonly buffer OverlayVertices {
    OverlayVertex vertices[];
};

layout(std430, binding = 27) readonly buffer OverlayIndices {
    uint indices[];
};

uniform mat4 uProjection;
uniform mat4 uTransform;
uniform vec2 uTranslation;
uniform int uFirstIndex;
uniform int uBaseVertex;

out vec4 vColor;
out vec2 vUV;

void main()
{
    OverlayVertex v = vertices[uBaseVertex + int(indices[uFirstIndex + gl_VertexID])];
    gl_Position = uProjection * (uTransform * vec4(vec2(v.x, v.y) + uTranslation, 0.0, 1.0));
    vColor = vec4(float(v.color & 0xFFu), float((v.color >> 8) & 0xFFu), float((v.color >> 16) & 0xFFu), float(v.color >> 24)) / 255.0;
    vUV = vec2(v.u, v.v);
}
