#version 430 core

// Camera matrices are filled in by the renderer (DrawCommand::bindCameraState).
uniform mat4 uProjection;
uniform mat4 uView;

out vec3 nearPoint;
out vec3 farPoint;

const vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

// For a fixed NDC depth the view-space depth is constant, so the unprojected point is affine in
// NDC xy - which makes interpolating these across the fullscreen triangle exact, for perspective
// and orthographic cameras alike.
vec3 Unproject(vec2 xy, float z, mat4 invViewProj)
{
    vec4 p = invViewProj * vec4(xy, z, 1.0);
    return p.xyz / p.w;
}

void main()
{
    vec2 p = positions[gl_VertexID];
    mat4 invViewProj = inverse(uProjection * uView);

    nearPoint = Unproject(p, -1.0, invViewProj);
    farPoint  = Unproject(p,  1.0, invViewProj);

    gl_Position = vec4(p, 0.0, 1.0);
}
