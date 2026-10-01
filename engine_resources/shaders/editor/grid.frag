#version 430 core

in vec3 nearPoint;
in vec3 farPoint;
out vec4 FragColor;

uniform mat4 uProjection;
uniform mat4 uView;

// Plane the grid lies on, named after the axis it is perpendicular to: 0 = YZ (normal X),
// 1 = XZ (normal Y, the "floor"), 2 = XY (normal Z).
uniform int axis;
// World-space size of one square.
uniform float cellSize;

const vec3 kGridColor = vec3(0.85);
const vec3 kAxisColors[3] = vec3[](
    vec3(0.86, 0.25, 0.25), // X
    vec3(0.45, 0.80, 0.25), // Y
    vec3(0.25, 0.47, 0.92)  // Z
);

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

    // Plane coordinates (a, b) and the world axes they run along.
    int axisA = (axis == 0) ? 1 : 0;
    int axisB = (axis == 2) ? 1 : 2;
    vec2 coord = vec2(world[axisA], world[axisB]);

    vec2 scaled = coord / cellSize;
    vec2 deriv = max(fwidth(scaled), vec2(1e-6));

    // Anti-aliased 1px lines at every cell border, and slightly thicker ones on the plane's axes.
    vec2 cellDist = abs(fract(scaled - 0.5) - 0.5) / deriv;
    float cellLine = 1.0 - min(min(cellDist.x, cellDist.y), 1.0);

    vec2 axisDist = abs(scaled) / deriv;
    float axisLineA = 1.0 - min(axisDist.x - 0.5, 1.0); // the line at a == 0 runs along axis B
    float axisLineB = 1.0 - min(axisDist.y - 0.5, 1.0); // the line at b == 0 runs along axis A
    axisLineA = clamp(axisLineA, 0.0, 1.0);
    axisLineB = clamp(axisLineB, 0.0, 1.0);

    // Squares smaller than a few pixels would shimmer into noise: fade the lines out as they get too dense.
    float density = max(deriv.x, deriv.y);
    float fade = 1.0 - smoothstep(0.1, 0.5, density);
    // And fade into the far plane instead of popping at it.
    fade *= 1.0 - smoothstep(0.85, 1.0, t);

    // Axis lines replace the cell line underneath them rather than being drawn over it, otherwise the
    // cell line's differently coloured anti-aliased edges show up as an outline around the axis line.
    float axisLine = max(axisLineA, axisLineB);
    vec3 axisColor = (axisLineA >= axisLineB) ? kAxisColors[axisB] : kAxisColors[axisA];

    float axisAlpha = axisLine * 0.9;
    float cellAlpha = cellLine * 0.45 * (1.0 - axisLine);

    float alpha = (axisAlpha + cellAlpha) * fade;
    if (alpha < 0.002)
        discard;

    // Straight (non-premultiplied) colour of the combined coverage.
    vec3 color = (axisColor * axisAlpha + kGridColor * cellAlpha) / (axisAlpha + cellAlpha);

    vec4 clip = uProjection * uView * vec4(world, 1.0);
    gl_FragDepth = clamp((clip.z / clip.w) * 0.5 + 0.5, 0.0, 1.0);

    FragColor = vec4(color, alpha);
}
