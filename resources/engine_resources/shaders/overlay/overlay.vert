#version 430 core

// The 2D overlay of shard.render on the Vulkan backend : the vertices and the indices are read from two storage buffers by gl_VertexID, there is no
// vertex buffer (the OpenGL backend has its own small program, see gl_overlay.cpp). A draw is a procedural draw of one vertex per index, from `uFirstIndex`.

struct OverlayVertex {
    float x, y;
    uint color;             // premultiplied alpha, r | g << 8 | b << 16 | a << 24
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
