#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "engine/core/math/hash.hpp"

namespace Shard::Engine::Core {

    // 64-bit name hash, constexpr. Collisions are possible in theory, Intern() reports them.
    class StringId {
    public:
        constexpr StringId() = default;
        constexpr StringId(std::string_view text) : m_Hash(Fnv1a64(text)) {}
        constexpr StringId(const char* text) : m_Hash(Fnv1a64(std::string_view(text))) {}
        static constexpr StringId FromHash(uint64_t hash) { StringId id; id.m_Hash = hash; return id; }

        static StringId Intern(std::string_view text);

        std::string ToString() const;

        constexpr uint64_t Value() const { return m_Hash; }
        constexpr bool IsValid() const { return m_Hash != 0; }

        static bool HadCollision();

        friend constexpr bool operator==(StringId a, StringId b) { return a.m_Hash == b.m_Hash; }
        friend constexpr bool operator!=(StringId a, StringId b) { return a.m_Hash != b.m_Hash; }
        friend constexpr bool operator<(StringId a, StringId b) { return a.m_Hash < b.m_Hash; }

    private:
        uint64_t m_Hash = 0;
    };

    namespace Literals {
        constexpr StringId operator""_sid(const char* text, size_t length) { return StringId(std::string_view(text, length)); }
    }
}

namespace std {
    template <>
    struct hash<Shard::Engine::Core::StringId> {
        size_t operator()(Shard::Engine::Core::StringId id) const noexcept { return static_cast<size_t>(id.Value()); }
    };
}
