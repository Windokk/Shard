#version 430 core

in vec3 nearPoint;
in vec3 farPoint;
out vec4 FragColor;

uniform mat4 uProjection;
uniform mat4 uView;

uniform int axis;
uniform float cellSize;

uniform vec3 gridColor;
uniform vec3 axisColorX;
uniform vec3 axisColorY;
uniform vec3 axisColorZ;

void main()
{
    vec3 rayDir = farPoint - nearPoint;
    float denom = rayDir[axis];
    if (abs(denom) < 1e-8)
        discard;

    float t = -nearPoint[axis] / denom;
    if (t <= 0.0 || t >= 1.0)
        discard;

    vec3 world = nearPoint + t * rayDir;

    int axisA = (axis == 0) ? 1 : 0;
    int axisB = (axis == 2) ? 1 : 2;
    vec2 coord = vec2(world[axisA], world[axisB]);

    vec2 scaled = coord / cellSize;
    vec2 deriv = max(fwidth(scaled), vec2(1e-6));

    vec2 cellDist = abs(fract(scaled - 0.5) - 0.5) / deriv;
    float cellLine = 1.0 - min(min(cellDist.x, cellDist.y), 1.0);

    vec2 axisDist = abs(scaled) / deriv;
    float axisLineA = 1.0 - min(axisDist.x - 0.5, 1.0);
    float axisLineB = 1.0 - min(axisDist.y - 0.5, 1.0);
    axisLineA = clamp(axisLineA, 0.0, 1.0);
    axisLineB = clamp(axisLineB, 0.0, 1.0);

    float density = max(deriv.x, deriv.y);
    float fade = 1.0 - smoothstep(0.1, 0.5, density);
    fade *= 1.0 - smoothstep(0.85, 1.0, t);

    // Axis lines replace the cell line under them, or the cell line's anti-aliased edges show as an outline.
    float axisLine = max(axisLineA, axisLineB);
    vec3 axisColors[3] = vec3[](axisColorX, axisColorY, axisColorZ);
    vec3 axisColor = (axisLineA >= axisLineB) ? axisColors[axisB] : axisColors[axisA];

    float axisAlpha = axisLine * 0.9;
    float cellAlpha = cellLine * 0.45 * (1.0 - axisLine);

    float alpha = (axisAlpha + cellAlpha) * fade;
    if (alpha < 0.002)
        discard;

    vec3 color = (axisColor * axisAlpha + gridColor * cellAlpha) / (axisAlpha + cellAlpha);

    vec4 clip = uProjection * uView * vec4(world, 1.0);
    gl_FragDepth = clamp((clip.z / clip.w) * 0.5 + 0.5, 0.0, 1.0);

    FragColor = vec4(color, alpha);
}
