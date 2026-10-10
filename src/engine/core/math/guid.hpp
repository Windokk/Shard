#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Shard::Engine::Core {

    /// @brief 128-bit globally unique identifier, written like a UUID : 8-4-4-4-12 hexadecimal digits.
    ///
    ///  * Generate() : random (UUID version 4). Two calls never give the same value in practice (122 random bits).
    ///  * FromName(ns, name) : deterministic, the same inputs always give the same Guid (assets named by their path,
    ///    generated content). A version-8 UUID (RFC 9562 "custom") : it is NOT the SHA-1 based version 5, so it does not
    ///    match the UUIDs other tools derive from the same names.
    ///
    /// The default-constructed Guid is all zeros = "no id" (IsNull).
    struct Guid {
        uint64_t high = 0;
        uint64_t low = 0;

        constexpr Guid() = default;
        constexpr Guid(uint64_t high, uint64_t low) : high(high), low(low) {}

        static Guid Generate();
        static Guid FromName(const Guid& ns, std::string_view name);
        static Guid FromName(std::string_view name);                 ///< in the engine's own namespace

        /// @brief Parses "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (case-insensitive, optional braces). nullopt otherwise.
        static std::optional<Guid> Parse(std::string_view text);

        std::string ToString() const;                                ///< lowercase, hyphenated
        constexpr bool IsNull() const { return high == 0 && low == 0; }
        constexpr explicit operator bool() const { return !IsNull(); }

        /// UUID version field (4 for Generate, 8 for FromName).
        constexpr int Version() const { return static_cast<int>((high >> 12) & 0xF); }

        friend constexpr bool operator==(const Guid& a, const Guid& b) { return a.high == b.high && a.low == b.low; }
        friend constexpr bool operator!=(const Guid& a, const Guid& b) { return !(a == b); }
        friend constexpr bool operator<(const Guid& a, const Guid& b) { return a.high != b.high ? a.high < b.high : a.low < b.low; }
    };
}

namespace std {
    template <>
    struct hash<Shard::Engine::Core::Guid> {
        size_t operator()(const Shard::Engine::Core::Guid& g) const noexcept {
            return static_cast<size_t>(g.high ^ (g.low * 0x9e3779b97f4a7c15ull));
        }
    };
}
