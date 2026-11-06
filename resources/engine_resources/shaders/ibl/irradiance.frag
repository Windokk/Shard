#version 430 core
out vec4 FragColor;
in vec3 WorldPos;

uniform samplerCube uEnvironmentMap;

const float PI = 3.14159265359;
const float MAX_RADIANCE = 500.0;

void main()
{		
    vec3 N = normalize(WorldPos);

    vec3 irradiance = vec3(0.0);   
    
    // Straight-up vector is parallel to a vertical normal, so cross() is 0 and normalize gives NaN.
    vec3 up    = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 right = normalize(cross(up, N));
    up         = normalize(cross(N, right));
       
    float sampleDelta = 0.025;
    float nrSamples = 0.0f;
    for(float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for(float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            vec3 tangentSample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N; 

            vec3 radiance = min(texture(uEnvironmentMap, sampleVec).rgb, vec3(MAX_RADIANCE));
            irradiance += radiance * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * irradiance * (1.0 / float(nrSamples));
    
    FragColor = vec4(irradiance, 1.0);
}