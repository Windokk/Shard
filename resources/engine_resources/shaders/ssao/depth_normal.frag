#version 430 core

in vec3 vViewNormal;

out vec3 gNormal;

void main()
{
    gNormal = normalize(vViewNormal);
}
