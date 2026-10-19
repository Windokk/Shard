#include <gtest/gtest.h>

#include "engine/plugins/crypto.hpp"

using namespace Shard::Engine::Plugins::Crypto;

namespace {
    template <size_t N>
    std::array<uint8_t, N> Bytes(const char* hex)
    {
        std::vector<uint8_t> v;
        EXPECT_TRUE(FromHex(hex, v, N));
        std::array<uint8_t, N> a{};
        std::copy(v.begin(), v.end(), a.begin());
        return a;
    }
}

TEST(Sha2, KnownDigests)
{
    EXPECT_EQ(ToHex(Sha256Of("abc", 3)), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(ToHex(Sha512Of("abc", 3)),
              "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    EXPECT_EQ(ToHex(Sha256Of("", 0)), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string thousand(1000, 'a');
    EXPECT_EQ(ToHex(Sha256Of(thousand.data(), thousand.size())), "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
    EXPECT_EQ(ToHex(Sha512Of(thousand.data(), thousand.size())),
              "67ba5535a46e3f86dbfbed8cbbaf0125c76ed549ff8b0b9e03e0c88cf90fa634fa7b12b47d77b694de488ace8d9a65967dc96df599727d3292a8d9d447709c97");
}

TEST(Sha2, StreamingGivesTheSameDigestWhateverTheChunks)
{
    std::string data;
    for (int i = 0; i < 5000; ++i) data += char(i * 7);
    const auto whole = Sha256Of(data.data(), data.size());
    Sha256 h;
    size_t at = 0;
    for (size_t chunk : { 1u, 63u, 64u, 65u, 127u, 1000u, 3000u }) {
        const size_t take = std::min(chunk, data.size() - at);
        h.Update(data.data() + at, take);
        at += take;
    }
    h.Update(data.data() + at, data.size() - at);
    EXPECT_EQ(h.Finish(), whole);
}

TEST(Hex, RoundTripAndRejections)
{
    std::vector<uint8_t> v;
    EXPECT_TRUE(FromHex("00ff10Ab", v));
    EXPECT_EQ(v, (std::vector<uint8_t>{ 0x00, 0xff, 0x10, 0xab }));
    EXPECT_EQ(ToHex(v.data(), v.size()), "00ff10ab");
    EXPECT_FALSE(FromHex("abc", v));
    EXPECT_FALSE(FromHex("zz", v));
    EXPECT_FALSE(FromHex("00ff", v, 3));
}

TEST(Ed25519, Rfc8032TestVectorOne)
{
    PublicKey pk;
    SecretKey sk;
    KeyPairFromSeed(Bytes<32>("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"), pk, sk);
    EXPECT_EQ(ToHex(pk), "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    const Signature sig = Sign("", 0, sk);
    EXPECT_EQ(ToHex(sig), "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    EXPECT_TRUE(Verify("", 0, sig, pk));
}

TEST(Ed25519, MatchesAnotherImplementationOnAnotherKey)
{
    std::array<uint8_t, 32> seed;
    for (int i = 0; i < 32; ++i) seed[size_t(i)] = uint8_t(i);
    PublicKey pk;
    SecretKey sk;
    KeyPairFromSeed(seed, pk, sk);
    EXPECT_EQ(ToHex(pk), "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8");
    const std::string message = "shard plugin signature test";
    const Signature sig = Sign(message.data(), message.size(), sk);
    EXPECT_EQ(ToHex(sig), "2d60ffbe3b0c3f660db29e3559e90ab4945c3413f29b71544558fbb9377a9ee40acb0e2517b0ffe0f7e1d9e4acf0d6df43ea27fd0a04a485464a858d3cc7c606");
    EXPECT_TRUE(Verify(message.data(), message.size(), sig, pk));
}

TEST(Ed25519, RefusesAWrongMessageKeyOrSignature)
{
    std::array<uint8_t, 32> seed{};
    seed[0] = 42;
    PublicKey pk, other;
    SecretKey sk, otherSk;
    KeyPairFromSeed(seed, pk, sk);
    seed[0] = 43;
    KeyPairFromSeed(seed, other, otherSk);

    const std::string message = "hello";
    Signature sig = Sign(message.data(), message.size(), sk);
    EXPECT_TRUE(Verify(message.data(), message.size(), sig, pk));
    EXPECT_FALSE(Verify("hellp", 5, sig, pk));
    EXPECT_FALSE(Verify(message.data(), message.size(), sig, other));
    for (size_t i : { 0u, 31u, 32u, 63u }) {
        Signature bad = sig;
        bad[i] ^= 1;
        EXPECT_FALSE(Verify(message.data(), message.size(), bad, pk)) << i;
    }
    PublicKey garbage{};
    garbage.fill(0xff);
    EXPECT_FALSE(Verify(message.data(), message.size(), sig, garbage));
}

TEST(Ed25519, ARandomSeedMakesAWorkingKey)
{
    std::array<uint8_t, 32> a, b;
    ASSERT_TRUE(RandomSeed(a));
    ASSERT_TRUE(RandomSeed(b));
    EXPECT_NE(a, b);
    PublicKey pk;
    SecretKey sk;
    KeyPairFromSeed(a, pk, sk);
    const Signature sig = Sign("x", 1, sk);
    EXPECT_TRUE(Verify("x", 1, sig, pk));
}

TEST(Ed25519, AMalleatedSignatureIsRefused)
{
    std::array<uint8_t, 32> seed{};
    seed[5] = 9;
    PublicKey pk;
    SecretKey sk;
    KeyPairFromSeed(seed, pk, sk);
    Signature sig = Sign("m", 1, sk);
    // S + L is the same point but not a canonical scalar
    static const uint8_t L[32] = { 0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10 };
    int carry = 0;
    for (int i = 0; i < 32; ++i) {
        const int v = sig[32 + size_t(i)] + L[i] + carry;
        sig[32 + size_t(i)] = uint8_t(v & 255);
        carry = v >> 8;
    }
    if (carry == 0)
        EXPECT_FALSE(Verify("m", 1, sig, pk));
}
