#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Shard::Engine::Plugins {

    struct SemVer {
        uint32_t major = 0;
        uint32_t minor = 0;
        uint32_t patch = 0;
        std::string prerelease;

        static std::optional<SemVer> Parse(std::string_view text);
        std::string ToString() const;

        int Compare(const SemVer& other) const;
        bool operator==(const SemVer& o) const { return Compare(o) == 0; }
        bool operator!=(const SemVer& o) const { return Compare(o) != 0; }
        bool operator<(const SemVer& o) const { return Compare(o) < 0; }
        bool operator<=(const SemVer& o) const { return Compare(o) <= 0; }
        bool operator>(const SemVer& o) const { return Compare(o) > 0; }
        bool operator>=(const SemVer& o) const { return Compare(o) >= 0; }
    };

    // Range syntax as npm/cargo: exact, ^, ~, comparators (all must hold), 1.x wildcards, "*", "a || b". A prerelease only satisfies a range naming a prerelease of the same major.minor.patch.
    class VersionRange {
    public:
        static std::optional<VersionRange> Parse(std::string_view text, std::string* error = nullptr);
        static VersionRange Any();

        bool Satisfies(const SemVer& version) const;
        const std::string& Text() const { return m_Text; }

    private:
        enum class Op : uint8_t { Eq, Lt, Le, Gt, Ge };
        struct Comparator { Op op; SemVer version; };
        using Set = std::vector<Comparator>;
        std::vector<Set> m_Alternatives;
        std::string m_Text;
    };
}
