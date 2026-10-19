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

    /// @brief Streaming SHA-256 / SHA-512 (FIPS 180-4)
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
    /// Hash of a file's bytes, hex (empty if the file can't be read)
    std::string Sha256FileHex(const std::string& path);

    std::string ToHex(const uint8_t* data, size_t size);
    template <size_t N> std::string ToHex(const std::array<uint8_t, N>& a) { return ToHex(a.data(), N); }
    /// @return false if the text is not hexadecimal of the length wanted (`bytes` ; 0 : any even length)
    bool FromHex(std::string_view text, std::vector<uint8_t>& out, size_t bytes = 0);

    /// @brief Ed25519 (RFC 8032) signatures
    using PublicKey = std::array<uint8_t, 32>;
    using SecretKey = std::array<uint8_t, 64>;       ///< seed (32) followed by the public key (32)
    using Signature = std::array<uint8_t, 64>;

    /// A key pair from a 32 byte seed (from a secure random source : the caller's job)
    void KeyPairFromSeed(const std::array<uint8_t, 32>& seed, PublicKey& publicKey, SecretKey& secretKey);
    Signature Sign(const void* message, size_t size, const SecretKey& secretKey);
    /// False for a bad signature, a malformed key, or a non canonical S
    bool Verify(const void* message, size_t size, const Signature& signature, const PublicKey& publicKey);

    /// 32 random bytes from the operating system (for a new key)
    bool RandomSeed(std::array<uint8_t, 32>& seed);
}
