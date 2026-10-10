#include "engine/core/math/guid.hpp"

#include <atomic>
#include <chrono>
#include <random>
#include <thread>

#include "engine/core/math/hash.hpp"
#include "engine/core/math/random.hpp"

namespace Shard::Engine::Core {

    namespace {
        // Stamps the RFC 4122 variant (10xx) and the version in the right bits
        Guid Stamp(uint64_t high, uint64_t low, uint64_t version) {
            high = (high & ~(0xFull << 12)) | (version << 12);
            low = (low & ~(0x3ull << 62)) | (0x2ull << 62);
            return Guid(high, low);
        }

        uint64_t EntropySeed() {
            // random_device is the real source ; the clock, a counter and the thread id are mixed in so that two
            // threads asking at the same instant, or a random_device that is deterministic (some old MinGW), still differ
            static std::atomic<uint64_t> counter{0};
            std::random_device device;
            uint64_t seed = (uint64_t(device()) << 32) | device();
            seed = HashCombine(seed, static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
            seed = HashCombine(seed, std::hash<std::thread::id>()(std::this_thread::get_id()));
            seed = HashCombine(seed, counter.fetch_add(1, std::memory_order_relaxed));
            return seed;
        }

        // 128 bits from a text : two FNV-1a runs with different seeds, each finished by a mixer
        Guid HashText(const Guid& ns, std::string_view text) {
            uint64_t a = Fnv1a64(text, 14695981039346656037ull ^ ns.high);
            uint64_t b = Fnv1a64(text, 0x84222325cbf29ce4ull ^ ns.low);
            a = Mix64(a ^ ns.low);
            b = Mix64(b ^ ns.high ^ (a << 1));
            return Guid(a, b);
        }

        int HexValue(char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }
    }

    Guid Guid::Generate() {
        SplitMix64 mix(EntropySeed());
        return Stamp(mix.Next(), mix.Next(), 4);
    }

    Guid Guid::FromName(const Guid& ns, std::string_view name) {
        const Guid h = HashText(ns, name);
        return Stamp(h.high, h.low, 8);
    }

    Guid Guid::FromName(std::string_view name) {
        // Fixed, arbitrary namespace of the engine
        static const Guid engineNamespace(0x5a1d5e2c9f3b4a67ull, 0x8c3e1b0d7f2a9e45ull);
        return FromName(engineNamespace, name);
    }

    std::optional<Guid> Guid::Parse(std::string_view text) {
        if (!text.empty() && text.front() == '{') {
            if (text.size() < 2 || text.back() != '}') return std::nullopt;
            text = text.substr(1, text.size() - 2);
        }
        if (text.size() != 36) return std::nullopt;

        uint64_t nibbles[2] = {0, 0};
        int count = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            const bool hyphen = (i == 8 || i == 13 || i == 18 || i == 23);
            if (hyphen) {
                if (text[i] != '-') return std::nullopt;
                continue;
            }
            const int v = HexValue(text[i]);
            if (v < 0) return std::nullopt;
            nibbles[count / 16] = (nibbles[count / 16] << 4) | static_cast<uint64_t>(v);
            ++count;
        }
        return Guid(nibbles[0], nibbles[1]);
    }

    std::string Guid::ToString() const {
        static const char* digits = "0123456789abcdef";
        std::string out;
        out.reserve(36);
        for (int i = 0; i < 32; ++i) {
            if (i == 8 || i == 12 || i == 16 || i == 20) out.push_back('-');
            const uint64_t word = i < 16 ? high : low;
            const int shift = (15 - (i % 16)) * 4;
            out.push_back(digits[(word >> shift) & 0xF]);
        }
        return out;
    }
}
