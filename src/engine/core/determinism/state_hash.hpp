#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/core/math/hash.hpp"

namespace Shard::Engine::Core::Determinism {

    // Fixed-size little-endian words, float bit patterns with -0.0 and NaN folded. Same hash on every platform. Not cryptographic.
    class StateHasher {
    public:
        StateHasher& Byte(uint8_t value) {
            m_State ^= value;
            m_State *= 1099511628211ull;
            return *this;
        }

        StateHasher& U32(uint32_t value) {
            for (int i = 0; i < 4; ++i) Byte(static_cast<uint8_t>(value >> (8 * i)));
            return *this;
        }

        StateHasher& U64(uint64_t value) {
            for (int i = 0; i < 8; ++i) Byte(static_cast<uint8_t>(value >> (8 * i)));
            return *this;
        }

        StateHasher& I32(int32_t value) { return U32(static_cast<uint32_t>(value)); }
        StateHasher& Bool(bool value) { return Byte(value ? 1 : 0); }

        StateHasher& Float(float value) {
            uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            if (bits == 0x80000000u) bits = 0;
            else if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu)) bits = 0x7fc00000u;
            return U32(bits);
        }

        StateHasher& Vec3(const glm::vec3& v) { return Float(v.x).Float(v.y).Float(v.z); }
        StateHasher& Vec4(const glm::vec4& v) { return Float(v.x).Float(v.y).Float(v.z).Float(v.w); }
        // q and -q are the same rotation, so fix the sign before hashing
        StateHasher& Quat(const glm::quat& q) {
            const bool flip = q.w < 0.0f || (q.w == 0.0f && (q.x < 0.0f || (q.x == 0.0f && (q.y < 0.0f || (q.y == 0.0f && q.z < 0.0f)))));
            const float s = flip ? -1.0f : 1.0f;
            return Float(q.x * s).Float(q.y * s).Float(q.z * s).Float(q.w * s);
        }

        // Length first, so "ab" + "c" and "a" + "bc" differ
        StateHasher& String(std::string_view text) {
            U64(text.size());
            for (char c : text) Byte(static_cast<uint8_t>(c));
            return *this;
        }

        uint64_t Digest() const { return Mix64(m_State); }

    private:
        uint64_t m_State = 14695981039346656037ull;
    };

    inline std::string HashToString(uint64_t hash) {
        static const char* digits = "0123456789abcdef";
        std::string out(16, '0');
        for (int i = 15; i >= 0; --i) { out[static_cast<size_t>(i)] = digits[hash & 0xF]; hash >>= 4; }
        return out;
    }

    inline bool HashFromString(std::string_view text, uint64_t& out) {
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text.remove_prefix(2);
        if (text.empty() || text.size() > 16) return false;
        uint64_t value = 0;
        for (char c : text) {
            int digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else return false;
            value = (value << 4) | static_cast<uint64_t>(digit);
        }
        out = value;
        return true;
    }
}
