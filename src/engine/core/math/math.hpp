#pragma once

#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Shard::Engine::Core {

    // ---- Types ------------------------------------------------------------------------------------------------
    // Single precision (float) is what the GPU and most of the engine use. Double precision is for WORLD positions :
    // a float has 24 bits of mantissa, so 10 km from the origin the smallest step is ~1 mm and at 100 km it is ~8 mm
    // (objects jitter), while a double stays under a micrometre across the solar system. Keep positions in double,
    // do the maths relative to a nearby origin (see ToLocal), and hand floats to the GPU.
    using Vec2 = glm::vec2;
    using Vec3 = glm::vec3;
    using Vec4 = glm::vec4;
    using Mat3 = glm::mat3;
    using Mat4 = glm::mat4;
    using Quat = glm::quat;

    using DVec2 = glm::dvec2;
    using DVec3 = glm::dvec3;
    using DVec4 = glm::dvec4;
    using DMat3 = glm::dmat3;
    using DMat4 = glm::dmat4;
    using DQuat = glm::dquat;

    namespace Math {

        inline constexpr double kPi = 3.14159265358979323846;
        inline constexpr double kTwoPi = 2.0 * kPi;
        inline constexpr double kHalfPi = 0.5 * kPi;
        inline constexpr float kPiF = static_cast<float>(kPi);

        template <typename T> constexpr T Radians(T degrees) { return degrees * static_cast<T>(kPi / 180.0); }
        template <typename T> constexpr T Degrees(T radians) { return radians * static_cast<T>(180.0 / kPi); }

        template <typename T> constexpr T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
        template <typename T> constexpr T Saturate(T v) { return Clamp<T>(v, T(0), T(1)); }
        template <typename T> constexpr T Lerp(T a, T b, T t) { return a + (b - a) * t; }

        /// Where v sits between a and b : 0 at a, 1 at b (not clamped). a == b gives 0.
        template <typename T> constexpr T InverseLerp(T a, T b, T v) { return a == b ? T(0) : (v - a) / (b - a); }

        /// Maps v from [inLo, inHi] to [outLo, outHi].
        template <typename T> constexpr T Remap(T v, T inLo, T inHi, T outLo, T outHi) {
            return Lerp(outLo, outHi, InverseLerp(inLo, inHi, v));
        }

        /// 0 below e0, 1 above e1, smooth S-curve in between (zero slope at both ends).
        template <typename T> constexpr T SmoothStep(T e0, T e1, T v) {
            const T t = Saturate(InverseLerp(e0, e1, v));
            return t * t * (T(3) - T(2) * t);
        }
        template <typename T> constexpr T SmootherStep(T e0, T e1, T v) {
            const T t = Saturate(InverseLerp(e0, e1, v));
            return t * t * t * (t * (t * T(6) - T(15)) + T(10));
        }

        /// |a - b| <= epsilon, or relative to the larger magnitude for big values.
        template <typename T> inline bool Approx(T a, T b, T epsilon = T(1e-5)) {
            const T diff = std::abs(a - b);
            return diff <= epsilon || diff <= epsilon * std::fmax(std::abs(a), std::abs(b));
        }

        /// v wrapped into [lo, hi) (angles, tile coordinates). Unlike %, works with negatives and floats.
        template <typename T> inline T Wrap(T v, T lo, T hi) {
            const T range = hi - lo;
            if (range <= T(0)) return lo;
            v = std::fmod(v - lo, range);
            return (v < T(0) ? v + range : v) + lo;
        }

        /// Next power of two >= v (v = 0 gives 1).
        constexpr uint32_t NextPowerOfTwo(uint32_t v) {
            if (v <= 1) return 1;
            --v;
            v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
            return v + 1;
        }
    }

    // ---- Large worlds ------------------------------------------------------------------------------------------

    /// @brief A world position (double) as a float offset from `origin` (double) : exact near the origin, so the
    /// GPU and the float maths see small numbers. Typical origin : the camera.
    inline Vec3 ToLocal(const DVec3& world, const DVec3& origin) { return Vec3(world - origin); }

    inline DVec3 ToWorld(const Vec3& local, const DVec3& origin) { return origin + DVec3(local); }

    /// @brief World matrix (double) -> float matrix relative to `origin` : the translation is subtracted in double
    /// BEFORE the narrowing, so a huge world position does not eat the float's precision.
    inline Mat4 ToLocal(const DMat4& world, const DVec3& origin) {
        DMat4 local = world;
        local[3] = DVec4(DVec3(world[3]) - origin, world[3].w);
        return Mat4(local);
    }

    /// @brief Translation * rotation * scale, in double.
    inline DMat4 ComposeTRS(const DVec3& translation, const DQuat& rotation, const DVec3& scale) {
        DMat4 m = glm::mat4_cast(rotation);
        m[0] *= scale.x;
        m[1] *= scale.y;
        m[2] *= scale.z;
        m[3] = DVec4(translation, 1.0);
        return m;
    }
}
