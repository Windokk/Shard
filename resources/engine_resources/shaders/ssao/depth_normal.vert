#version 430 core

layout(location = 0) in vec3 aPos;
layout(location = 2) in vec3 aNormal;

#include "../common/skinning.glsl"

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 vViewNormal;

void main()
{
    vec3 localPos = aPos;
    vec3 localNormal = aNormal;
#ifdef SKINNED
    mat4 skin = SkinMatrix();
    localPos = (skin * vec4(localPos, 1.0)).xyz;
    localNormal = mat3(skin) * localNormal;
#endif

    mat3 viewModel = mat3(view * model);
    vViewNormal = normalize(viewModel * localNormal);

    gl_Position = projection * view * model * vec4(localPos, 1.0);
}
