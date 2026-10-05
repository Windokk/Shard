#version 430 core
layout (location = 0) in vec3 aPos;

out vec3 WorldPos;

uniform mat4 uProjection;
uniform mat4 uView;

void main()
{
    WorldPos = aPos;  
    gl_Position =  uProjection * uView * vec4(WorldPos, 1.0);
}