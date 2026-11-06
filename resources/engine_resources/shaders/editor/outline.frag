#version 430 core

in vec2 texCoords;
out vec4 FragColor;

// Explicit bindings, units come from layout(binding=N). BindPassData assigns slots in alphabetical order of customSamplers (std::map): maskTex = 0, seedTex = 1.
layout(binding = 0) uniform sampler2D maskTex;
layout(binding = 1) uniform sampler2D seedTex;
uniform vec3 outlineColor;
uniform float outlineThickness;

void main()
{
    float mask = texture(maskTex, texCoords).r;

    if (mask > 0.5)
        discard;

    vec4 seed = texture(seedTex, texCoords);

    if (seed.z < 0.5)
        discard;

    float dist = distance(gl_FragCoord.xy, seed.xy);

    if (dist > outlineThickness)
        discard;

    FragColor = vec4(outlineColor, 1.0);
}
