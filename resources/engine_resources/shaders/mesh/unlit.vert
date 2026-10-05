#version 430 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec4 aColor;

uniform mat4 model;
uniform mat4 uProjection;
uniform mat4 uView;

out vec2 texCoord;
out vec4 color;
out vec3 worldPos;
out vec3 worldNormal;

void main()
{
    texCoord = aTexCoord;
    color = aColor;
    
    vec4 world = model * vec4(aPos, 1.0);
    worldPos = world.xyz;

    worldNormal = normalize(mat3(model) * aNormal);

    gl_Position = uProjection * uView * world;
}