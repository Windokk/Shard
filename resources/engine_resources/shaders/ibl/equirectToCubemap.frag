#version 430 core
out vec4 FragColor;
in vec3 WorldPos;

uniform sampler2D uEquirectangularMap;

const vec2 invAtan = vec2(0.1591, 0.3183);
vec2 SampleSphericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main()
{		
    vec2 uv = SampleSphericalMap(normalize(WorldPos));
    vec3 color = texture(uEquirectangularMap, uv).rgb;

    // The cubemap is 16 bit floats, which end at 65504 : a sun disc of a real HDR sky is past it (and becomes an infinity
    // that the convolutions below spread over the whole hemisphere), so the sky is kept to what the format can hold
    color = clamp(color, vec3(0.0), vec3(60000.0));
    
    FragColor = vec4(color, 1.0);
}