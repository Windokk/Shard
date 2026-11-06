#version 430 core
in vec4 FragPos;

uniform vec4 lightPos;
uniform float farPlane;

void main()
{
    float lightDistance = length(FragPos.xyz - lightPos.xyz);
    
    lightDistance = lightDistance / farPlane;
    
    gl_FragDepth = lightDistance;
}