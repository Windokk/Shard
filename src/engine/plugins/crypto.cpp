#include "engine/plugins/crypto.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <random>

namespace Shard::Engine::Plugins::Crypto {

    namespace {
        typedef int64_t gf[16];

        
static const uint32_t K256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u };
static const uint32_t H256[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
static const uint64_t K512[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
    0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
    0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
    0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
    0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
    0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
    0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
    0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
    0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
    0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
    0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
    0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
    0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull };
static const uint64_t H512[8] = {
    0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
    0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull };
static const gf kD = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070, 0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
static const gf kD2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0, 0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
static const gf kX = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c, 0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
static const gf kY = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
static const gf kI = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43, 0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};
static const int64_t kL[32] = {237, 211, 245, 92, 26, 99, 18, 88, 214, 156, 247, 162, 222, 249, 222, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16};


        inline uint32_t Rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
        inline uint64_t Rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }
    }

    // ---- SHA-256 ----

    Sha256::Sha256()
    {
        std::memcpy(m_State, H256, sizeof(m_State));
    }

    void Sha256::Block(const uint8_t* b)
    {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(b[i * 4]) << 24) | (uint32_t(b[i * 4 + 1]) << 16) | (uint32_t(b[i * 4 + 2]) << 8) | uint32_t(b[i * 4 + 3]);
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = Rotr32(w[i - 15], 7) ^ Rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Rotr32(w[i - 2], 17) ^ Rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = m_State[0], bb = m_State[1], c = m_State[2], d = m_State[3], e = m_State[4], f = m_State[5], g = m_State[6], h = m_State[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = Rotr32(e, 6) ^ Rotr32(e, 11) ^ Rotr32(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = h + S1 + ch + K256[i] + w[i];
            const uint32_t S0 = Rotr32(a, 2) ^ Rotr32(a, 13) ^ Rotr32(a, 22);
            const uint32_t maj = (a & bb) ^ (a & c) ^ (bb & c);
            const uint32_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
        }
        m_State[0] += a; m_State[1] += bb; m_State[2] += c; m_State[3] += d;
        m_State[4] += e; m_State[5] += f; m_State[6] += g; m_State[7] += h;
    }

    void Sha256::Update(const void* data, size_t size)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        m_Length += size;
        while (size > 0) {
            const size_t take = std::min(size, sizeof(m_Buffer) - m_Used);
            std::memcpy(m_Buffer + m_Used, p, take);
            m_Used += take; p += take; size -= take;
            if (m_Used == sizeof(m_Buffer)) { Block(m_Buffer); m_Used = 0; }
        }
    }

    Sha256Digest Sha256::Finish()
    {
        const uint64_t bits = m_Length * 8;
        const uint8_t pad = 0x80;
        Update(&pad, 1);
        const uint8_t zero = 0;
        while (m_Used != 56) Update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = uint8_t(bits >> (56 - 8 * i));
        Update(len, 8);
        Sha256Digest out;
        for (int i = 0; i < 8; ++i) {
            out[i * 4] = uint8_t(m_State[i] >> 24); out[i * 4 + 1] = uint8_t(m_State[i] >> 16);
            out[i * 4 + 2] = uint8_t(m_State[i] >> 8); out[i * 4 + 3] = uint8_t(m_State[i]);
        }
        return out;
    }

    // ---- SHA-512 ----

    Sha512::Sha512()
    {
        std::memcpy(m_State, H512, sizeof(m_State));
    }

    void Sha512::Block(const uint8_t* b)
    {
        uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            uint64_t v = 0;
            for (int j = 0; j < 8; ++j) v = (v << 8) | b[i * 8 + j];
            w[i] = v;
        }
        for (int i = 16; i < 80; ++i) {
            const uint64_t s0 = Rotr64(w[i - 15], 1) ^ Rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const uint64_t s1 = Rotr64(w[i - 2], 19) ^ Rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint64_t a = m_State[0], bb = m_State[1], c = m_State[2], d = m_State[3], e = m_State[4], f = m_State[5], g = m_State[6], h = m_State[7];
        for (int i = 0; i < 80; ++i) {
            const uint64_t S1 = Rotr64(e, 14) ^ Rotr64(e, 18) ^ Rotr64(e, 41);
            const uint64_t ch = (e & f) ^ (~e & g);
            const uint64_t t1 = h + S1 + ch + K512[i] + w[i];
            const uint64_t S0 = Rotr64(a, 28) ^ Rotr64(a, 34) ^ Rotr64(a, 39);
            const uint64_t maj = (a & bb) ^ (a & c) ^ (bb & c);
            const uint64_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
        }
        m_State[0] += a; m_State[1] += bb; m_State[2] += c; m_State[3] += d;
        m_State[4] += e; m_State[5] += f; m_State[6] += g; m_State[7] += h;
    }

    void Sha512::Update(const void* data, size_t size)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        m_Length += size;
        while (size > 0) {
            const size_t take = std::min(size, sizeof(m_Buffer) - m_Used);
            std::memcpy(m_Buffer + m_Used, p, take);
            m_Used += take; p += take; size -= take;
            if (m_Used == sizeof(m_Buffer)) { Block(m_Buffer); m_Used = 0; }
        }
    }

    Sha512Digest Sha512::Finish()
    {
        const uint64_t bits = m_Length * 8;      // messages here are far below 2^61 bytes, so the high 64 bits of the length are zero
        const uint8_t pad = 0x80;
        Update(&pad, 1);
        const uint8_t zero = 0;
        while (m_Used != 112) Update(&zero, 1);
        uint8_t len[16] = {};
        for (int i = 0; i < 8; ++i) len[8 + i] = uint8_t(bits >> (56 - 8 * i));
        Update(len, 16);
        Sha512Digest out;
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j) out[i * 8 + j] = uint8_t(m_State[i] >> (56 - 8 * j));
        return out;
    }

    Sha256Digest Sha256Of(const void* data, size_t size) { Sha256 h; h.Update(data, size); return h.Finish(); }
    Sha512Digest Sha512Of(const void* data, size_t size) { Sha512 h; h.Update(data, size); return h.Finish(); }

    std::string Sha256FileHex(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return {};
        Sha256 h;
        std::vector<char> buffer(1 << 16);
        while (in) {
            in.read(buffer.data(), std::streamsize(buffer.size()));
            const std::streamsize got = in.gcount();
            if (got > 0) h.Update(buffer.data(), size_t(got));
        }
        return ToHex(h.Finish());
    }

    std::string ToHex(const uint8_t* data, size_t size)
    {
        static const char* digits = "0123456789abcdef";
        std::string out;
        out.reserve(size * 2);
        for (size_t i = 0; i < size; ++i) { out += digits[data[i] >> 4]; out += digits[data[i] & 15]; }
        return out;
    }

    bool FromHex(std::string_view text, std::vector<uint8_t>& out, size_t bytes)
    {
        if (text.size() % 2 != 0 || (bytes != 0 && text.size() != bytes * 2)) return false;
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        out.clear();
        for (size_t i = 0; i < text.size(); i += 2) {
            const int hi = nibble(text[i]), lo = nibble(text[i + 1]);
            if (hi < 0 || lo < 0) return false;
            out.push_back(uint8_t(hi * 16 + lo));
        }
        return true;
    }

    // ---- Ed25519 (after TweetNaCl : sixteen 16 bit limbs, constant time field arithmetic) ----

    namespace {
        void Set25519(gf r, const gf a) { for (int i = 0; i < 16; ++i) r[i] = a[i]; }

        void Car25519(gf o)
        {
            for (int i = 0; i < 16; ++i) {
                o[i] += (int64_t(1) << 16);
                const int64_t c = o[i] >> 16;
                o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
                o[i] -= c * (int64_t(1) << 16);
            }
        }

        void Sel25519(gf p, gf q, int b)
        {
            const int64_t c = ~(int64_t(b) - 1);
            for (int i = 0; i < 16; ++i) {
                const int64_t t = c & (p[i] ^ q[i]);
                p[i] ^= t;
                q[i] ^= t;
            }
        }

        void Pack25519(uint8_t* o, const gf n)
        {
            gf m, t;
            for (int i = 0; i < 16; ++i) t[i] = n[i];
            Car25519(t); Car25519(t); Car25519(t);
            for (int j = 0; j < 2; ++j) {
                m[0] = t[0] - 0xffed;
                for (int i = 1; i < 15; ++i) {
                    m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
                    m[i - 1] &= 0xffff;
                }
                m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
                const int b = int((m[15] >> 16) & 1);
                m[14] &= 0xffff;
                Sel25519(t, m, 1 - b);
            }
            for (int i = 0; i < 16; ++i) {
                o[2 * i] = uint8_t(t[i] & 0xff);
                o[2 * i + 1] = uint8_t(t[i] >> 8);
            }
        }

        int Verify32(const uint8_t* x, const uint8_t* y)
        {
            uint32_t d = 0;
            for (int i = 0; i < 32; ++i) d |= x[i] ^ y[i];
            return (1 & ((d - 1) >> 8)) - 1;
        }

        int Neq25519(const gf a, const gf b)
        {
            uint8_t c[32], d[32];
            Pack25519(c, a);
            Pack25519(d, b);
            return Verify32(c, d);
        }

        uint8_t Par25519(const gf a)
        {
            uint8_t d[32];
            Pack25519(d, a);
            return d[0] & 1;
        }

        void Unpack25519(gf o, const uint8_t* n)
        {
            for (int i = 0; i < 16; ++i) o[i] = n[2 * i] + (int64_t(n[2 * i + 1]) << 8);
            o[15] &= 0x7fff;
        }

        void A(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i]; }
        void Z(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i]; }

        void M(gf o, const gf a, const gf b)
        {
            int64_t t[31];
            for (int i = 0; i < 31; ++i) t[i] = 0;
            for (int i = 0; i < 16; ++i)
                for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
            for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
            for (int i = 0; i < 16; ++i) o[i] = t[i];
            Car25519(o);
            Car25519(o);
        }

        void S(gf o, const gf a) { M(o, a, a); }

        void Inv25519(gf o, const gf i)
        {
            gf c;
            for (int a = 0; a < 16; ++a) c[a] = i[a];
            for (int a = 253; a >= 0; --a) {
                S(c, c);
                if (a != 2 && a != 4) M(c, c, i);
            }
            for (int a = 0; a < 16; ++a) o[a] = c[a];
        }

        void Pow2523(gf o, const gf i)
        {
            gf c;
            for (int a = 0; a < 16; ++a) c[a] = i[a];
            for (int a = 250; a >= 0; --a) {
                S(c, c);
                if (a != 1) M(c, c, i);
            }
            for (int a = 0; a < 16; ++a) o[a] = c[a];
        }

        const gf kGf0 = {0};
        const gf kGf1 = {1};

        void Add(gf p[4], gf q[4])
        {
            gf a, b, c, d, t, e, f, g, h;
            Z(a, p[1], p[0]);
            Z(t, q[1], q[0]);
            M(a, a, t);
            A(b, p[0], p[1]);
            A(t, q[0], q[1]);
            M(b, b, t);
            M(c, p[3], q[3]);
            M(c, c, kD2);
            M(d, p[2], q[2]);
            A(d, d, d);
            Z(e, b, a);
            Z(f, d, c);
            A(g, d, c);
            A(h, b, a);
            M(p[0], e, f);
            M(p[1], h, g);
            M(p[2], g, f);
            M(p[3], e, h);
        }

        void CSwap(gf p[4], gf q[4], uint8_t b)
        {
            for (int i = 0; i < 4; ++i) Sel25519(p[i], q[i], b);
        }

        void Pack(uint8_t* r, gf p[4])
        {
            gf tx, ty, zi;
            Inv25519(zi, p[2]);
            M(tx, p[0], zi);
            M(ty, p[1], zi);
            Pack25519(r, ty);
            r[31] ^= uint8_t(Par25519(tx) << 7);
        }

        void ScalarMult(gf p[4], gf q[4], const uint8_t* s)
        {
            Set25519(p[0], kGf0);
            Set25519(p[1], kGf1);
            Set25519(p[2], kGf1);
            Set25519(p[3], kGf0);
            for (int i = 255; i >= 0; --i) {
                const uint8_t b = (s[i / 8] >> (i & 7)) & 1;
                CSwap(p, q, b);
                Add(q, p);
                Add(p, p);
                CSwap(p, q, b);
            }
        }

        void ScalarBase(gf p[4], const uint8_t* s)
        {
            gf q[4];
            Set25519(q[0], kX);
            Set25519(q[1], kY);
            Set25519(q[2], kGf1);
            M(q[3], kX, kY);
            ScalarMult(p, q, s);
        }

        void ModL(uint8_t* r, int64_t x[64])
        {
            int64_t carry;
            int i, j;
            for (i = 63; i >= 32; --i) {
                carry = 0;
                for (j = i - 32; j < i - 12; ++j) {
                    x[j] += carry - 16 * x[i] * kL[j - (i - 32)];
                    carry = (x[j] + 128) >> 8;
                    x[j] -= carry * 256;
                }
                x[j] += carry;
                x[i] = 0;
            }
            carry = 0;
            for (j = 0; j < 32; ++j) {
                x[j] += carry - (x[31] >> 4) * kL[j];
                carry = x[j] >> 8;
                x[j] &= 255;
            }
            for (j = 0; j < 32; ++j) x[j] -= carry * kL[j];
            for (i = 0; i < 32; ++i) {
                x[i + 1] += x[i] >> 8;
                r[i] = uint8_t(x[i] & 255);
            }
        }

        void Reduce(uint8_t* r)
        {
            int64_t x[64];
            for (int i = 0; i < 64; ++i) x[i] = int64_t(uint64_t(r[i]));
            for (int i = 0; i < 64; ++i) r[i] = 0;
            ModL(r, x);
        }

        int UnpackNeg(gf r[4], const uint8_t p[32])
        {
            gf t, chk, num, den, den2, den4, den6;
            Set25519(r[2], kGf1);
            Unpack25519(r[1], p);
            S(num, r[1]);
            M(den, num, kD);
            Z(num, num, r[2]);
            A(den, r[2], den);

            S(den2, den);
            S(den4, den2);
            M(den6, den4, den2);
            M(t, den6, num);
            M(t, t, den);

            Pow2523(t, t);
            M(t, t, num);
            M(t, t, den);
            M(t, t, den);
            M(r[0], t, den);

            S(chk, r[0]);
            M(chk, chk, den);
            if (Neq25519(chk, num)) M(r[0], r[0], kI);

            S(chk, r[0]);
            M(chk, chk, den);
            if (Neq25519(chk, num)) return -1;

            if (Par25519(r[0]) == (p[31] >> 7)) Z(r[0], kGf0, r[0]);

            M(r[3], r[0], r[1]);
            return 0;
        }

        // s < L ?
        bool ScalarIsCanonical(const uint8_t* s)
        {
            for (int i = 31; i >= 0; --i) {
                if (s[i] < kL[i]) return true;
                if (s[i] > kL[i]) return false;
            }
            return false;           // equal to L
        }
    }

    void KeyPairFromSeed(const std::array<uint8_t, 32>& seed, PublicKey& publicKey, SecretKey& secretKey)
    {
        Sha512Digest d = Sha512Of(seed.data(), 32);
        d[0] &= 248;
        d[31] &= 127;
        d[31] |= 64;
        gf p[4];
        ScalarBase(p, d.data());
        Pack(publicKey.data(), p);
        std::memcpy(secretKey.data(), seed.data(), 32);
        std::memcpy(secretKey.data() + 32, publicKey.data(), 32);
    }

    Signature Sign(const void* message, size_t n, const SecretKey& sk)
    {
        Sha512Digest d = Sha512Of(sk.data(), 32);
        d[0] &= 248;
        d[31] &= 127;
        d[31] |= 64;

        // r = H(prefix || M) mod L
        uint8_t r[64];
        {
            Sha512 h;
            h.Update(d.data() + 32, 32);
            h.Update(message, n);
            const Sha512Digest rh = h.Finish();
            std::memcpy(r, rh.data(), 64);
            Reduce(r);
        }

        Signature sig;
        gf p[4];
        ScalarBase(p, r);
        Pack(sig.data(), p);                 // R

        // k = H(R || A || M) mod L
        uint8_t k[64];
        {
            Sha512 h;
            h.Update(sig.data(), 32);
            h.Update(sk.data() + 32, 32);
            h.Update(message, n);
            const Sha512Digest kh = h.Finish();
            std::memcpy(k, kh.data(), 64);
            Reduce(k);
        }

        int64_t x[64];
        for (int i = 0; i < 64; ++i) x[i] = 0;
        for (int i = 0; i < 32; ++i) x[i] = int64_t(uint64_t(r[i]));
        for (int i = 0; i < 32; ++i)
            for (int j = 0; j < 32; ++j) x[i + j] += int64_t(k[i]) * int64_t(uint64_t(d[j]));
        ModL(sig.data() + 32, x);            // S = r + k * a mod L
        return sig;
    }

    bool Verify(const void* message, size_t n, const Signature& sig, const PublicKey& pk)
    {
        if (!ScalarIsCanonical(sig.data() + 32)) return false;

        gf p[4], q[4];
        if (UnpackNeg(q, pk.data())) return false;

        uint8_t h[64];
        {
            Sha512 hash;
            hash.Update(sig.data(), 32);
            hash.Update(pk.data(), 32);
            hash.Update(message, n);
            const Sha512Digest hd = hash.Finish();
            std::memcpy(h, hd.data(), 64);
            Reduce(h);
        }

        ScalarMult(p, q, h);
        ScalarBase(q, sig.data() + 32);
        Add(p, q);
        uint8_t t[32];
        Pack(t, p);
        return Verify32(sig.data(), t) == 0;
    }

    bool RandomSeed(std::array<uint8_t, 32>& seed)
    {
        try {
            std::random_device device;
            for (size_t i = 0; i < seed.size(); i += 4) {
                const uint32_t v = device();
                for (size_t j = 0; j < 4 && i + j < seed.size(); ++j) seed[i + j] = uint8_t(v >> (8 * j));
            }
            return true;
        } catch (...) {
            return false;
        }
    }
}
