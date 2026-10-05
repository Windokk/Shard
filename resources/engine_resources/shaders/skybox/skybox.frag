#version 330 core

in vec2 UV;
out vec4 FragColor;

uniform samplerCube uSkybox;

// Inverse matrices
uniform mat4 uProjection;
uniform mat4 uView;
uniform bool uIsOrtho;

void main()
{
    mat4 invProj = inverse(uProjection);
    mat4 invView = inverse(uView);

    invView = mat4(mat3(invView));

    vec2 ndc = UV * 2.0 - 1.0;

    vec3 dir;

    if (!uIsOrtho)
    {
        vec4 clip = vec4(ndc, 1.0, 1.0);
        vec4 view = invProj * clip;
        view = vec4(view.xy, -1.0, 0.0);

        dir = normalize((invView * view).xyz);
    }
    else
    {
        // In ortho, direction is constant (camera forward)
        vec3 forward = normalize((invView * vec4(0, 0, -1, 0)).xyz);
        vec3 right   = normalize((invView * vec4(1, 0, 0, 0)).xyz);
        vec3 up      = normalize((invView * vec4(0, 1, 0, 0)).xyz);

        dir = normalize(forward + ndc.x * right + ndc.y * up);
    }

    FragColor = texture(uSkybox, dir);
}