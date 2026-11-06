#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Shard::Engine::Plugins::Crypto {

    using Sha256Digest = std::array<uint8_t, 32>;
    using Sha512Digest = std::array<uint8_t, 64>;

    class Sha256 {
    public:
        Sha256();
        void Update(const void* data, size_t size);
        Sha256Digest Finish();
    private:
        void Block(const uint8_t* block);
        uint32_t m_State[8];
        uint8_t m_Buffer[64];
        uint64_t m_Length = 0;
        size_t m_Used = 0;
    };

    class Sha512 {
    public:
        Sha512();
        void Update(const void* data, size_t size);
        Sha512Digest Finish();
    private:
        void Block(const uint8_t* block);
        uint64_t m_State[8];
        uint8_t m_Buffer[128];
        uint64_t m_Length = 0;
        size_t m_Used = 0;
    };

    Sha256Digest Sha256Of(const void* data, size_t size);
    Sha512Digest Sha512Of(const void* data, size_t size);
    std::string Sha256FileHex(const std::string& path);

    std::string ToHex(const uint8_t* data, size_t size);
    template <size_t N> std::string ToHex(const std::array<uint8_t, N>& a) { return ToHex(a.data(), N); }
    bool FromHex(std::string_view text, std::vector<uint8_t>& out, size_t bytes = 0);

    using PublicKey = std::array<uint8_t, 32>;
    using SecretKey = std::array<uint8_t, 64>;
    using Signature = std::array<uint8_t, 64>;

    void KeyPairFromSeed(const std::array<uint8_t, 32>& seed, PublicKey& publicKey, SecretKey& secretKey);
    Signature Sign(const void* message, size_t size, const SecretKey& secretKey);
    // False for a bad signature, malformed key or non-canonical S
    bool Verify(const void* message, size_t size, const Signature& signature, const PublicKey& publicKey);

    bool RandomSeed(std::array<uint8_t, 32>& seed);
}
