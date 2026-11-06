#version 430 core

layout(location = 0) in vec3 aPos;
layout(location = 2) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 vViewNormal;

void main()
{
    mat3 viewModel = mat3(view * model);
    vViewNormal = normalize(viewModel * aNormal);

    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
