#version 430 core

out vec4 fragColor;

in vec2 texCoord;
in vec4 color;

uniform sampler2D tex1;

void main(){
    float alpha = texture(tex1, texCoord).r;
    fragColor = vec4(color.rgb, color.a * alpha);
}