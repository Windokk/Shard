#version 430 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec4 aColor;
layout (location = 4) in vec3 aTangent;

uniform mat4 model;
uniform mat4 uProjection;
uniform mat4 uView;

out vec2 texCoord;
out vec4 color;
out vec3 worldPos;
out mat4 viewMatrix;
out vec3 T;
out vec3 B;
out vec3 N;


void main()
{
    vec3 T_world = normalize(mat3(model) * aTangent);
    vec3 N_world = normalize(mat3(model) * aNormal);
    vec3 B_world = normalize(cross(N_world, T_world));

    T = T_world;
    B = B_world;
    N = N_world;

    texCoord = aTexCoord;
    color = aColor;
    viewMatrix = uView;

    vec4 world = model * vec4(aPos, 1.0);
    worldPos = world.xyz;

    gl_Position = (uProjection * uView) * world;
}