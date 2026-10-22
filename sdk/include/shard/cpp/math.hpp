// Shard C++ SDK : the small math a gameplay script needs, on the plain structs of the ABI (Vec2, Vec3, Vec4, Quat, Color, Pose).
//
// The types are aggregates (Vec3{ 1, 2, 3 }), laid out like the engine's own, so a field of a script or a component can be one. The functions are
// free functions in namespace shard; the usual operators work. Conventions are the engine's: right handed, Y up, an actor looks down -Z, angles in
// radians (the Degrees helpers convert), quaternions x, y, z, w.
#pragma once

#include <cmath>

#include <shard/cpp/component.hpp>

namespace shard {

    constexpr float kPi = 3.14159265358979323846f;
    // The factors are written out in double and rounded once : kPi / 180.f divides a float that is already rounded, and is off by a unit of the last
    // place, which is not the factor the engine (and glm) multiplies by
    constexpr float Radians(float degrees) { return degrees * 0.017453292519943295f; }
    constexpr float Degrees(float radians) { return radians * 57.29577951308232f; }
    template <typename T> constexpr T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
    constexpr float Lerp(float a, float b, float t) { return a + (b - a) * t; }

    // ---- Vec2 ----
    constexpr Vec2 operator+(Vec2 a, Vec2 b) { return { a.x + b.x, a.y + b.y }; }
    constexpr Vec2 operator-(Vec2 a, Vec2 b) { return { a.x - b.x, a.y - b.y }; }
    constexpr Vec2 operator*(Vec2 a, float k) { return { a.x * k, a.y * k }; }
    constexpr Vec2 operator*(float k, Vec2 a) { return a * k; }
    constexpr Vec2 operator/(Vec2 a, float k) { return { a.x / k, a.y / k }; }
    constexpr Vec2 operator-(Vec2 a) { return { -a.x, -a.y }; }
    constexpr float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
    inline float Length(Vec2 a) { return std::sqrt(Dot(a, a)); }

    // ---- Vec3 ----
    constexpr Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    constexpr Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    constexpr Vec3 operator*(Vec3 a, float k) { return { a.x * k, a.y * k, a.z * k }; }
    constexpr Vec3 operator*(float k, Vec3 a) { return a * k; }
    constexpr Vec3 operator*(Vec3 a, Vec3 b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
    constexpr Vec3 operator/(Vec3 a, float k) { return { a.x / k, a.y / k, a.z / k }; }
    constexpr Vec3 operator-(Vec3 a) { return { -a.x, -a.y, -a.z }; }
    inline Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }
    inline Vec3& operator-=(Vec3& a, Vec3 b) { a = a - b; return a; }
    inline Vec3& operator*=(Vec3& a, float k) { a = a * k; return a; }
    constexpr bool operator==(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
    constexpr bool operator!=(Vec3 a, Vec3 b) { return !(a == b); }
    constexpr float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    constexpr Vec3 Cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    constexpr float LengthSquared(Vec3 a) { return Dot(a, a); }
    inline float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
    inline float Distance(Vec3 a, Vec3 b) { return Length(a - b); }
    /// The unit vector with that direction (the zero vector stays zero)
    inline Vec3 Normalize(Vec3 a) { const float l = Length(a); return l > 1e-12f ? a / l : Vec3{ 0.f, 0.f, 0.f }; }
    constexpr Vec3 Lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
    /// `v` moved toward `target` by at most `maxDelta`
    inline Vec3 MoveTowards(Vec3 v, Vec3 target, float maxDelta)
    {
        const Vec3 d = target - v;
        const float l = Length(d);
        return l <= maxDelta || l < 1e-12f ? target : v + d / l * maxDelta;
    }

    // ---- Quat ----
    constexpr Quat operator*(Quat a, Quat b)
    {
        return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                 a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                 a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                 a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
    }
    constexpr Quat Conjugate(Quat q) { return { -q.x, -q.y, -q.z, q.w }; }
    inline Quat Normalize(Quat q)
    {
        const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        return l > 1e-12f ? Quat{ q.x / l, q.y / l, q.z / l, q.w / l } : Quat{ 0.f, 0.f, 0.f, 1.f };
    }
    /// The vector rotated by the quaternion
    constexpr Vec3 operator*(Quat q, Vec3 v)
    {
        const Vec3 u{ q.x, q.y, q.z };
        const Vec3 t = Cross(u, v) * 2.f;
        return v + t * q.w + Cross(u, t);
    }
    inline Quat AxisAngle(Vec3 axis, float radians)
    {
        const Vec3 a = Normalize(axis);
        const float s = std::sin(radians * 0.5f);
        return { a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5f) };
    }
    /// Rotations about X (pitch), Y (yaw) then Z (roll), in degrees: applied in the order yaw, pitch, roll as a first person camera expects
    inline Quat FromEulerDegrees(float pitch, float yaw, float roll)
    {
        return AxisAngle({ 0.f, 1.f, 0.f }, Radians(yaw)) * AxisAngle({ 1.f, 0.f, 0.f }, Radians(pitch)) * AxisAngle({ 0.f, 0.f, 1.f }, Radians(roll));
    }
    /// The rotation that turns an actor's forward (-Z) toward `direction`, keeping `up` up
    inline Quat LookRotation(Vec3 direction, Vec3 up = { 0.f, 1.f, 0.f })
    {
        const Vec3 f = Normalize(direction);
        if (LengthSquared(f) < 0.5f) return { 0.f, 0.f, 0.f, 1.f };
        Vec3 r = Normalize(Cross(f, up));
        if (LengthSquared(r) < 0.5f) r = { 1.f, 0.f, 0.f };
        const Vec3 u = Cross(r, f);
        // The matrix with columns right, up, back (= -forward), as a quaternion
        const float m00 = r.x, m01 = u.x, m02 = -f.x, m10 = r.y, m11 = u.y, m12 = -f.y, m20 = r.z, m21 = u.z, m22 = -f.z;
        const float trace = m00 + m11 + m22;
        if (trace > 0.f) {
            const float s = std::sqrt(trace + 1.f) * 2.f;
            return Normalize({ (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s });
        }
        if (m00 > m11 && m00 > m22) {
            const float s = std::sqrt(1.f + m00 - m11 - m22) * 2.f;
            return Normalize({ 0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s });
        }
        if (m11 > m22) {
            const float s = std::sqrt(1.f + m11 - m00 - m22) * 2.f;
            return Normalize({ (m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s });
        }
        const float s = std::sqrt(1.f + m22 - m00 - m11) * 2.f;
        return Normalize({ (m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s });
    }
    inline Quat Slerp(Quat a, Quat b, float t)
    {
        float cosine = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        if (cosine < 0.f) { b = { -b.x, -b.y, -b.z, -b.w }; cosine = -cosine; }
        if (cosine > 0.9995f) return Normalize({ Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t), Lerp(a.w, b.w, t) });
        const float angle = std::acos(cosine);
        const float sa = std::sin((1.f - t) * angle) / std::sin(angle), sb = std::sin(t * angle) / std::sin(angle);
        return { a.x * sa + b.x * sb, a.y * sa + b.y * sb, a.z * sa + b.z * sb, a.w * sa + b.w * sb };
    }

    /// An actor's pose: the layout of ShardLocalTransform (position, rotation, scale)
    struct Pose {
        Vec3 position;
        Quat rotation;
        Vec3 scale{ 1.f, 1.f, 1.f };

        /// The directions of an actor with this rotation (it looks down -Z)
        Vec3 Forward() const { return rotation * Vec3{ 0.f, 0.f, -1.f }; }
        Vec3 Right() const { return rotation * Vec3{ 1.f, 0.f, 0.f }; }
        Vec3 Up() const { return rotation * Vec3{ 0.f, 1.f, 0.f }; }
    };
    static_assert(sizeof(Pose) == sizeof(ShardLocalTransform), "Pose is a ShardLocalTransform");
}
