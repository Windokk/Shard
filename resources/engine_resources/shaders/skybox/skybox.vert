#version 330 core

out vec2 UV;

void main()
{
    // Fullscreen triangle
    vec2 pos = vec2(
        (gl_VertexID == 2) ?  3.0 : -1.0,
        (gl_VertexID == 1) ?  3.0 : -1.0
    );

    UV = pos * 0.5 + 0.5; // map to 0..1
    gl_Position = vec4(pos, 0.0, 1.0);
}