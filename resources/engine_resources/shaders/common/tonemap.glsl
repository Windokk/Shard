// The display transform : linear HDR scene light -> the colours a monitor shows. See display_transform.hpp, whose C++
// twin of these functions has to stay identical (same constants, same formulas).
//
//   exposure (a linear factor, 2^EV)  ->  tonemapper (the curve)  ->  the sRGB transfer curve  ->  a little dither
//
// Values of `tonemapper` mirror Tonemapper (display_transform.hpp).
const int TONEMAP_NONE     = 0;
const int TONEMAP_ACES     = 1;
const int TONEMAP_NEUTRAL  = 2;
const int TONEMAP_REINHARD = 3;

vec3 LinearToSRGB(vec3 linear)
{
    linear = clamp(linear, 0.0, 1.0);
    vec3 low = linear * 12.92;
    vec3 high = 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(linear, vec3(0.0031308)));
}

vec3 SRGBToLinear(vec3 encoded)
{
    encoded = clamp(encoded, 0.0, 1.0);
    vec3 low = encoded / 12.92;
    vec3 high = pow((encoded + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, lessThanEqual(encoded, vec3(0.04045)));
}

// Hill's fit of the ACES reference rendering transform and output transform (matrices written by columns)
vec3 TonemapACES(vec3 color)
{
    const mat3 inputMatrix = mat3(vec3(0.59719, 0.07600, 0.02840), vec3(0.35458, 0.90834, 0.13383), vec3(0.04823, 0.01566, 0.83777));
    const mat3 outputMatrix = mat3(vec3(1.60475, -0.10208, -0.00327), vec3(-0.53108, 1.10813, -0.07276), vec3(-0.07367, -0.00605, 1.07602));

    vec3 v = inputMatrix * color;
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return clamp(outputMatrix * (a / b), 0.0, 1.0);
}

// Khronos PBR Neutral tone mapping (Apache 2.0, github.com/KhronosGroup/ToneMapping)
vec3 TonemapNeutral(vec3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
        return clamp(color, 0.0, 1.0);

    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return clamp(mix(color, vec3(newPeak), g), 0.0, 1.0);
}

vec3 TonemapReinhard(vec3 color)
{
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return clamp(color / (1.0 + luminance), 0.0, 1.0);
}

// Linear scene light (any range) -> linear light in [0, 1] : the curve alone
vec3 ApplyTonemap(vec3 color, int tonemapper)
{
    color = max(color, vec3(0.0));

    if (tonemapper == TONEMAP_ACES)
        return TonemapACES(color);
    if (tonemapper == TONEMAP_NEUTRAL)
        return TonemapNeutral(color);
    if (tonemapper == TONEMAP_REINHARD)
        return TonemapReinhard(color);
    return clamp(color, 0.0, 1.0);
}

// The whole transform of a pixel, without the dither : what the 8 bit display target stores
vec3 DisplayTransform(vec3 hdr, int tonemapper, float exposureScale)
{
    return LinearToSRGB(ApplyTonemap(hdr * exposureScale, tonemapper));
}

// Interleaved gradient noise (Jimenez 2014) in [0, 1) : one value per pixel, no visible pattern
float InterleavedGradientNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Half a level of 8 bit noise added before the quantization turns the banding of a smooth gradient (a sky, a soft shadow)
// into grain no one can see
vec3 DitherForDisplay(vec3 encoded, vec2 pixel)
{
    return encoded + vec3((InterleavedGradientNoise(pixel) - 0.5) / 255.0);
}
