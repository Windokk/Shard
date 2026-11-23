#version 430 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec4 aColor;
layout (location = 4) in vec3 aTangent;

#include "../common/skinning.glsl"

uniform mat4 model;
uniform mat4 uProjection;
uniform mat4 uView;

void main()
{
#ifdef SKINNED
    vec3 localPos = (SkinMatrix() * vec4(aPos, 1.0)).xyz;
#else
    vec3 localPos = aPos;
#endif
    gl_Position = uProjection * uView * model * vec4(localPos, 1.0);
}