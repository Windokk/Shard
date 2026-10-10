#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "engine/core/math/hash.hpp"

namespace Shard::Engine::Core {

    /// @brief A name reduced to a 64-bit hash : comparing, hashing and storing it costs one integer, and the
    /// constexpr constructor computes it at compile time (`constexpr StringId kAlbedo("albedo");` can be used in a
    /// `switch`). Use it where strings are keys (asset names, parameters, events, tags) and the text itself is rarely needed.
    ///
    /// Two different names could in theory share a hash (probability ~1e-19 per pair). Intern() registers the text and
    /// reports a collision ; ToString() gives the text back for StringIds that were interned (logs, debuggers).
    class StringId {
    public:
        constexpr StringId() = default;
        constexpr StringId(std::string_view text) : m_Hash(Fnv1a64(text)) {}
        constexpr StringId(const char* text) : m_Hash(Fnv1a64(std::string_view(text))) {}
        static constexpr StringId FromHash(uint64_t hash) { StringId id; id.m_Hash = hash; return id; }

        /// @brief Same value as StringId(text), and remembers the text so ToString() can return it. If another text
        /// with the same hash was already interned, HadCollision() becomes true (and debug builds assert).
        static StringId Intern(std::string_view text);

        /// @brief The text if it was interned, otherwise "#<hex hash>".
        std::string ToString() const;

        constexpr uint64_t Value() const { return m_Hash; }
        /// The default-constructed id (hash 0) is "no name".
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
