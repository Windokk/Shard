#version 430 core

out vec4 fragColor;

in vec2 texCoord;
in vec4 color;
in vec3 worldPos;
in vec3 worldNormal;

uniform bool useTexture;
uniform sampler2D diffuse;
uniform bool useCustomColor;
uniform vec4 customColor;

uniform bool masked;

void main(){
    if(useTexture){
        vec4 baseColor = texture(diffuse, texCoord);
        
        if (masked && baseColor.a < 0.5)
            discard;

        fragColor = baseColor;
    }
    else if(useCustomColor){
        
        if (masked && customColor.a < 0.5)
            discard;

        fragColor = customColor;
    }
    else{
        fragColor = color;
    }
}