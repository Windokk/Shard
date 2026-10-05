#version 450

// Full-screen triangle, identical to fb/framebuffer.vert and editor/outline.vert - duplicated rather
// than shared because ResourcesManager::GetShader("shaders/ssao/ssao") requires a .vert file living
// next to ssao.frag at that exact path (see ResourcesManager::GetShader), the same reason
// editor/outline.vert already duplicates this instead of pointing back at fb/framebuffer.vert.
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
