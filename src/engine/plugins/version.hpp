#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Shard::Engine::Plugins {

    /// @brief A semantic version : major.minor.patch, optionally "-prerelease" ("1.4.0-beta.2"). Build metadata ("+abc") is accepted and ignored.
    struct SemVer {
        uint32_t major = 0;
        uint32_t minor = 0;
        uint32_t patch = 0;
        std::string prerelease;

        static std::optional<SemVer> Parse(std::string_view text);
        std::string ToString() const;

        /// Precedence as semver defines it : a prerelease is lower than its release, prerelease identifiers compare one by one
        int Compare(const SemVer& other) const;
        bool operator==(const SemVer& o) const { return Compare(o) == 0; }
        bool operator!=(const SemVer& o) const { return Compare(o) != 0; }
        bool operator<(const SemVer& o) const { return Compare(o) < 0; }
        bool operator<=(const SemVer& o) const { return Compare(o) <= 0; }
        bool operator>(const SemVer& o) const { return Compare(o) > 0; }
        bool operator>=(const SemVer& o) const { return Compare(o) >= 0; }
    };

    /// @brief A set of acceptable versions, written the way npm and cargo do :
    ///   "1.2.3" / "=1.2.3"  exactly that version        "^1.2.3"   >=1.2.3 <2.0.0 (<0.3.0 for 0.2.x, <0.0.4 for 0.0.3)
    ///   "~1.2.3"            >=1.2.3 <1.3.0              ">=1.2 <2" comparators, all of which have to hold
    ///   "1.x" / "1.2.*"     wildcard                    "*" or ""  anything
    /// "a || b" accepts either side. A prerelease only satisfies a range that names a prerelease of the same major.minor.patch.
    class VersionRange {
    public:
        /// @return nullopt (and `error`) if the text is not a valid range
        static std::optional<VersionRange> Parse(std::string_view text, std::string* error = nullptr);
        static VersionRange Any();

        bool Satisfies(const SemVer& version) const;
        const std::string& Text() const { return m_Text; }

    private:
        enum class Op : uint8_t { Eq, Lt, Le, Gt, Ge };
        struct Comparator { Op op; SemVer version; };
        using Set = std::vector<Comparator>;     // all of them must hold
        std::vector<Set> m_Alternatives;          // any set may hold
        std::string m_Text;
    };
}
