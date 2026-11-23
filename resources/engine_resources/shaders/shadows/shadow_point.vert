#version 430 core

layout(location = 0) in vec3 aPos;

#include "../common/skinning.glsl"

uniform mat4 model;

void main() {
#ifdef SKINNED
    vec3 localPos = (SkinMatrix() * vec4(aPos, 1.0)).xyz;
#else
    vec3 localPos = aPos;
#endif
    gl_Position = model * vec4(localPos, 1.0);
}