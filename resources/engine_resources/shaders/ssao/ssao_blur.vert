#version 450

// Full-screen triangle - see the identical comment in ssao.vert for why this is a duplicate rather than
// a shared file.
const vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

const vec2 uvs[3] = vec2[](
    vec2(0.0, 0.0),
    vec2(2.0, 0.0),
    vec2(0.0, 2.0)
);

out vec2 texCoords;

void main()
{
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    texCoords = uvs[gl_VertexID];
}
