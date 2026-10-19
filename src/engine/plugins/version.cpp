#include "engine/plugins/version.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace Shard::Engine::Plugins {

    namespace {
        std::string_view Trim(std::string_view s)
        {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
            return s;
        }

        bool ParseNumber(std::string_view s, uint32_t& out)
        {
            if (s.empty() || s.size() > 9) return false;
            if (s.size() > 1 && s.front() == '0') return false;     // no leading zeros
            uint32_t v = 0;
            auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
            if (ec != std::errc() || ptr != s.data() + s.size()) return false;
            out = v;
            return true;
        }

        bool IsNumeric(const std::string& s)
        {
            return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
        }

        std::vector<std::string> Split(std::string_view s, char sep)
        {
            std::vector<std::string> parts;
            size_t start = 0;
            while (true) {
                const size_t at = s.find(sep, start);
                if (at == std::string_view::npos) { parts.emplace_back(s.substr(start)); break; }
                parts.emplace_back(s.substr(start, at - start));
                start = at + 1;
            }
            return parts;
        }
    }

    std::optional<SemVer> SemVer::Parse(std::string_view text)
    {
        text = Trim(text);
        if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.remove_prefix(1);
        if (const size_t plus = text.find('+'); plus != std::string_view::npos) text = text.substr(0, plus);

        SemVer v;
        if (const size_t dash = text.find('-'); dash != std::string_view::npos) {
            v.prerelease = std::string(text.substr(dash + 1));
            if (v.prerelease.empty()) return std::nullopt;
            for (const std::string& id : Split(v.prerelease, '.')) {
                if (id.empty()) return std::nullopt;
                if (!std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '-'; })) return std::nullopt;
            }
            text = text.substr(0, dash);
        }
        const std::vector<std::string> parts = Split(text, '.');
        if (parts.size() != 3) return std::nullopt;
        if (!ParseNumber(parts[0], v.major) || !ParseNumber(parts[1], v.minor) || !ParseNumber(parts[2], v.patch)) return std::nullopt;
        return v;
    }

    std::string SemVer::ToString() const
    {
        std::string s = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
        if (!prerelease.empty()) s += "-" + prerelease;
        return s;
    }

    int SemVer::Compare(const SemVer& o) const
    {
        if (major != o.major) return major < o.major ? -1 : 1;
        if (minor != o.minor) return minor < o.minor ? -1 : 1;
        if (patch != o.patch) return patch < o.patch ? -1 : 1;
        if (prerelease.empty() && o.prerelease.empty()) return 0;
        if (prerelease.empty()) return 1;           // a release is above its prereleases
        if (o.prerelease.empty()) return -1;
        const std::vector<std::string> a = Split(prerelease, '.');
        const std::vector<std::string> b = Split(o.prerelease, '.');
        for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
            const bool an = IsNumeric(a[i]), bn = IsNumeric(b[i]);
            if (an && bn) {
                const unsigned long x = std::stoul(a[i]), y = std::stoul(b[i]);
                if (x != y) return x < y ? -1 : 1;
            } else if (an != bn) {
                return an ? -1 : 1;                 // numeric identifiers are lower than alphanumeric ones
            } else if (a[i] != b[i]) {
                return a[i] < b[i] ? -1 : 1;
            }
        }
        if (a.size() == b.size()) return 0;
        return a.size() < b.size() ? -1 : 1;
    }

    VersionRange VersionRange::Any()
    {
        VersionRange r;
        r.m_Alternatives.push_back({ Comparator{ Op::Ge, SemVer{} } });
        r.m_Text = "*";
        return r;
    }

    namespace {
        // A possibly partial version : "1", "1.2", "1.2.3", with "x", "X" or "*" for a wildcard component
        struct Partial {
            int count = 0;              // how many leading components are given (0 = anything)
            uint32_t v[3] = { 0, 0, 0 };
            std::string prerelease;
        };

        bool ParsePartial(std::string_view text, Partial& out)
        {
            text = Trim(text);
            if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.remove_prefix(1);
            if (text.empty() || text == "*" || text == "x" || text == "X") { out.count = 0; return true; }
            std::string pre;
            if (const size_t dash = text.find('-'); dash != std::string_view::npos) {
                pre = std::string(text.substr(dash + 1));
                text = text.substr(0, dash);
            }
            const std::vector<std::string> parts = Split(text, '.');
            if (parts.size() > 3) return false;
            out.count = 0;
            for (size_t i = 0; i < parts.size(); ++i) {
                if (parts[i] == "x" || parts[i] == "X" || parts[i] == "*") break;
                if (!ParseNumber(parts[i], out.v[i])) return false;
                out.count = int(i) + 1;
            }
            if (!pre.empty()) {
                if (out.count != 3) return false;
                out.prerelease = pre;
            }
            return true;
        }

        SemVer Make(uint32_t a, uint32_t b, uint32_t c, std::string pre = {})
        {
            SemVer v; v.major = a; v.minor = b; v.patch = c; v.prerelease = std::move(pre);
            return v;
        }
    }

    std::optional<VersionRange> VersionRange::Parse(std::string_view text, std::string* error)
    {
        auto fail = [&](const std::string& why) -> std::optional<VersionRange> {
            if (error) *error = "'" + std::string(text) + "' is not a valid version range : " + why;
            return std::nullopt;
        };

        VersionRange range;
        range.m_Text = std::string(Trim(text));
        if (range.m_Text.empty()) return Any();

        size_t from = 0;
        std::vector<std::string> alternatives;
        while (true) {
            const size_t at = range.m_Text.find("||", from);
            if (at == std::string::npos) { alternatives.push_back(range.m_Text.substr(from)); break; }
            alternatives.push_back(range.m_Text.substr(from, at - from));
            from = at + 2;
        }

        for (const std::string& alt : alternatives) {
            Set set;
            // Tokens split on spaces ; a comparator may be written apart from its version (">= 1.2")
            std::vector<std::string> tokens;
            {
                std::string cur;
                for (char c : alt) {
                    if (std::isspace(static_cast<unsigned char>(c))) { if (!cur.empty()) tokens.push_back(cur), cur.clear(); }
                    else cur += c;
                }
                if (!cur.empty()) tokens.push_back(cur);
                for (size_t i = 0; i + 1 < tokens.size();) {
                    const std::string& t = tokens[i];
                    if (t == ">=" || t == "<=" || t == ">" || t == "<" || t == "=" || t == "^" || t == "~") {
                        tokens[i] += tokens[i + 1];
                        tokens.erase(tokens.begin() + long(i) + 1);
                    } else ++i;
                }
            }
            if (tokens.empty()) { set.push_back({ Op::Ge, SemVer{} }); range.m_Alternatives.push_back(std::move(set)); continue; }

            for (std::string tok : tokens) {
                std::string_view t = tok;
                enum class Kind { Plain, Caret, Tilde, Cmp } kind = Kind::Plain;
                Op op = Op::Eq;
                if (t.substr(0, 2) == ">=") { kind = Kind::Cmp; op = Op::Ge; t.remove_prefix(2); }
                else if (t.substr(0, 2) == "<=") { kind = Kind::Cmp; op = Op::Le; t.remove_prefix(2); }
                else if (t.substr(0, 1) == ">") { kind = Kind::Cmp; op = Op::Gt; t.remove_prefix(1); }
                else if (t.substr(0, 1) == "<") { kind = Kind::Cmp; op = Op::Lt; t.remove_prefix(1); }
                else if (t.substr(0, 1) == "=") { kind = Kind::Plain; t.remove_prefix(1); }
                else if (t.substr(0, 1) == "^") { kind = Kind::Caret; t.remove_prefix(1); }
                else if (t.substr(0, 1) == "~") { kind = Kind::Tilde; t.remove_prefix(1); }

                Partial p;
                if (!ParsePartial(t, p)) return fail("bad version '" + std::string(t) + "'");

                const SemVer low = Make(p.v[0], p.v[1], p.v[2], p.prerelease);
                if (kind == Kind::Cmp) {
                    if (p.count == 0) {
                        // ">=*" is everything, "<*" nothing : only the first is useful
                        if (op == Op::Ge || op == Op::Le) set.push_back({ Op::Ge, SemVer{} });
                        else return fail("a comparator needs a version");
                    } else if (op == Op::Gt && p.count < 3) {
                        // >1.2 means >=1.3.0
                        set.push_back({ Op::Ge, p.count == 1 ? Make(p.v[0] + 1, 0, 0) : Make(p.v[0], p.v[1] + 1, 0) });
                    } else if (op == Op::Le && p.count < 3) {
                        set.push_back({ Op::Lt, p.count == 1 ? Make(p.v[0] + 1, 0, 0) : Make(p.v[0], p.v[1] + 1, 0) });
                    } else {
                        set.push_back({ op, low });
                    }
                } else if (kind == Kind::Caret) {
                    if (p.count == 0) { set.push_back({ Op::Ge, SemVer{} }); continue; }
                    set.push_back({ Op::Ge, low });
                    SemVer high;
                    if (p.v[0] > 0 || p.count == 1) high = Make(p.v[0] + 1, 0, 0);
                    else if (p.v[1] > 0 || p.count == 2) high = Make(0, p.v[1] + 1, 0);
                    else high = Make(0, 0, p.v[2] + 1);
                    set.push_back({ Op::Lt, high });
                } else if (kind == Kind::Tilde) {
                    if (p.count == 0) { set.push_back({ Op::Ge, SemVer{} }); continue; }
                    set.push_back({ Op::Ge, low });
                    set.push_back({ Op::Lt, p.count == 1 ? Make(p.v[0] + 1, 0, 0) : Make(p.v[0], p.v[1] + 1, 0) });
                } else {
                    // Plain / wildcard
                    if (p.count == 0) set.push_back({ Op::Ge, SemVer{} });
                    else if (p.count == 3) set.push_back({ Op::Eq, low });
                    else {
                        set.push_back({ Op::Ge, low });
                        set.push_back({ Op::Lt, p.count == 1 ? Make(p.v[0] + 1, 0, 0) : Make(p.v[0], p.v[1] + 1, 0) });
                    }
                }
            }
            range.m_Alternatives.push_back(std::move(set));
        }
        return range;
    }

    bool VersionRange::Satisfies(const SemVer& version) const
    {
        for (const Set& set : m_Alternatives) {
            bool ok = true;
            bool prereleaseAllowed = version.prerelease.empty();
            for (const Comparator& c : set) {
                if (!c.version.prerelease.empty() && c.version.major == version.major && c.version.minor == version.minor && c.version.patch == version.patch)
                    prereleaseAllowed = true;
                const int cmp = version.Compare(c.version);
                switch (c.op) {
                    case Op::Eq: ok = ok && cmp == 0; break;
                    case Op::Lt: ok = ok && cmp < 0; break;
                    case Op::Le: ok = ok && cmp <= 0; break;
                    case Op::Gt: ok = ok && cmp > 0; break;
                    case Op::Ge: ok = ok && cmp >= 0; break;
                }
            }
            if (ok && prereleaseAllowed) return true;
        }
        return false;
    }
}
