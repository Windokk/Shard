#include "engine/plugins/archive.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace Shard::Engine::Plugins {

    namespace fs = std::filesystem;

    uint32_t Crc32(const void* data, size_t size, uint32_t previous)
    {
        static uint32_t table[256];
        static bool built = false;
        if (!built) {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                table[i] = c;
            }
            built = true;
        }
        uint32_t crc = ~previous;
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
        return ~crc;
    }

    // ---- Inflate (RFC 1951), after zlib's "puff" ----

    namespace {
        constexpr int kMaxBits = 15;
        constexpr int kMaxLCodes = 286;
        constexpr int kMaxDCodes = 30;
        constexpr int kFixLCodes = 288;

        struct State {
            const uint8_t* in;
            size_t inLen;
            size_t inPos = 0;
            uint32_t bitBuf = 0;
            int bitCnt = 0;
            std::vector<uint8_t>* out;
            size_t maxOut;
            bool error = false;
        };

        struct Huffman {
            short count[kMaxBits + 1];
            short symbol[kFixLCodes];
        };

        int Bits(State& s, int need)
        {
            long val = s.bitBuf;
            while (s.bitCnt < need) {
                if (s.inPos >= s.inLen) { s.error = true; return 0; }
                val |= long(s.in[s.inPos++]) << s.bitCnt;
                s.bitCnt += 8;
            }
            s.bitBuf = uint32_t(val >> need);
            s.bitCnt -= need;
            return int(val & ((1L << need) - 1));
        }

        int Decode(State& s, const Huffman& h)
        {
            int code = 0, first = 0, index = 0;
            for (int len = 1; len <= kMaxBits; ++len) {
                code |= Bits(s, 1);
                if (s.error) return -1;
                const int count = h.count[len];
                if (code - count < first) return h.symbol[index + (code - first)];
                index += count;
                first += count;
                first <<= 1;
                code <<= 1;
            }
            return -10;
        }

        int Construct(Huffman& h, const short* length, int n)
        {
            for (int len = 0; len <= kMaxBits; ++len) h.count[len] = 0;
            for (int symbol = 0; symbol < n; ++symbol) h.count[length[symbol]]++;
            if (h.count[0] == n) return 0;
            int left = 1;
            for (int len = 1; len <= kMaxBits; ++len) {
                left <<= 1;
                left -= h.count[len];
                if (left < 0) return left;
            }
            short offs[kMaxBits + 1];
            offs[1] = 0;
            for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = short(offs[len] + h.count[len]);
            for (int symbol = 0; symbol < n; ++symbol)
                if (length[symbol] != 0) h.symbol[offs[length[symbol]]++] = short(symbol);
            return left;
        }

        bool Codes(State& s, const Huffman& lencode, const Huffman& distcode)
        {
            static const short lens[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
            static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
            static const short dists[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
            static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
            for (;;) {
                int symbol = Decode(s, lencode);
                if (s.error || symbol < 0) return false;
                if (symbol < 256) {
                    if (s.out->size() >= s.maxOut) return false;
                    s.out->push_back(uint8_t(symbol));
                } else if (symbol == 256) {
                    return true;
                } else {
                    symbol -= 257;
                    if (symbol >= 29) return false;
                    const int len = lens[symbol] + Bits(s, lext[symbol]);
                    const int dsym = Decode(s, distcode);
                    if (s.error || dsym < 0 || dsym >= 30) return false;
                    const size_t dist = size_t(dists[dsym] + Bits(s, dext[dsym]));
                    if (s.error || dist > s.out->size()) return false;
                    if (s.out->size() + size_t(len) > s.maxOut) return false;
                    size_t from = s.out->size() - dist;
                    for (int i = 0; i < len; ++i) s.out->push_back((*s.out)[from + size_t(i)]);
                }
            }
        }

        bool Stored(State& s)
        {
            s.bitBuf = 0;
            s.bitCnt = 0;
            if (s.inPos + 4 > s.inLen) return false;
            unsigned len = s.in[s.inPos] | (s.in[s.inPos + 1] << 8);
            const unsigned nlen = s.in[s.inPos + 2] | (s.in[s.inPos + 3] << 8);
            s.inPos += 4;
            if (len != (~nlen & 0xffff)) return false;
            if (s.inPos + len > s.inLen) return false;
            if (s.out->size() + len > s.maxOut) return false;
            s.out->insert(s.out->end(), s.in + s.inPos, s.in + s.inPos + len);
            s.inPos += len;
            return true;
        }

        bool Fixed(State& s)
        {
            static Huffman lencode, distcode;
            static bool built = false;
            if (!built) {
                short lengths[kFixLCodes];
                int symbol = 0;
                for (; symbol < 144; ++symbol) lengths[symbol] = 8;
                for (; symbol < 256; ++symbol) lengths[symbol] = 9;
                for (; symbol < 280; ++symbol) lengths[symbol] = 7;
                for (; symbol < kFixLCodes; ++symbol) lengths[symbol] = 8;
                Construct(lencode, lengths, kFixLCodes);
                for (symbol = 0; symbol < kMaxDCodes; ++symbol) lengths[symbol] = 5;
                Construct(distcode, lengths, kMaxDCodes);
                built = true;
            }
            return Codes(s, lencode, distcode);
        }

        bool Dynamic(State& s)
        {
            static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            const int nlen = Bits(s, 5) + 257;
            const int ndist = Bits(s, 5) + 1;
            const int ncode = Bits(s, 4) + 4;
            if (s.error || nlen > kMaxLCodes || ndist > kMaxDCodes) return false;
            short lengths[kMaxLCodes + kMaxDCodes];
            int index = 0;
            for (; index < ncode; ++index) lengths[order[index]] = short(Bits(s, 3));
            for (; index < 19; ++index) lengths[order[index]] = 0;
            Huffman lencode, distcode;
            if (s.error || Construct(lencode, lengths, 19) != 0) return false;
            index = 0;
            while (index < nlen + ndist) {
                int symbol = Decode(s, lencode);
                if (s.error || symbol < 0) return false;
                if (symbol < 16) {
                    lengths[index++] = short(symbol);
                } else {
                    int len = 0, rep;
                    if (symbol == 16) {
                        if (index == 0) return false;
                        len = lengths[index - 1];
                        rep = 3 + Bits(s, 2);
                    } else if (symbol == 17) {
                        rep = 3 + Bits(s, 3);
                    } else {
                        rep = 11 + Bits(s, 7);
                    }
                    if (s.error || index + rep > nlen + ndist) return false;
                    while (rep--) lengths[index++] = short(len);
                }
            }
            if (lengths[256] == 0) return false;
            int err = Construct(lencode, lengths, nlen);
            if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) return false;
            err = Construct(distcode, lengths + nlen, ndist);
            if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) return false;
            return Codes(s, lencode, distcode);
        }
    }

    Core::Result<std::vector<uint8_t>> Inflate(const uint8_t* data, size_t size, size_t maxOutput)
    {
        std::vector<uint8_t> out;
        State s;
        s.in = data;
        s.inLen = size;
        s.out = &out;
        s.maxOut = maxOutput;
        int last;
        do {
            last = Bits(s, 1);
            const int type = Bits(s, 2);
            if (s.error) return Core::Err("deflate : the data ends too soon");
            bool ok;
            switch (type) {
                case 0: ok = Stored(s); break;
                case 1: ok = Fixed(s); break;
                case 2: ok = Dynamic(s); break;
                default: return Core::Err("deflate : bad block type");
            }
            if (!ok) return Core::Err(out.size() >= maxOutput ? "deflate : the data is larger than allowed" : "deflate : corrupt data");
        } while (!last);
        return Core::Ok(std::move(out));
    }

    // ---- Paths ----

    bool IsSafeArchivePath(const std::string& path)
    {
        if (path.empty() || path.size() > 1024) return false;
        if (path[0] == '/' || path.find('\\') != std::string::npos || path.find(':') != std::string::npos) return false;
        if (path.find('\0') != std::string::npos) return false;
        size_t start = 0;
        while (start <= path.size()) {
            size_t end = path.find('/', start);
            if (end == std::string::npos) end = path.size();
            const std::string part = path.substr(start, end - start);
            if (part == ".." || part == ".") return false;
            if (end == path.size()) break;
            start = end + 1;
        }
        return true;
    }

    // ---- ZipArchive ----

    namespace {
        uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
        uint32_t U32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
    }

    Core::Result<ZipArchive> ZipArchive::Open(const std::string& path, const ArchiveLimits& limits)
    {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in) return Core::Err("cannot open '" + path + "'");
        const std::streamsize size = in.tellg();
        if (size < 22) return Core::Err("'" + path + "' is not a zip archive");
        if (uint64_t(size) > 4ull * 1024 * 1024 * 1024 - 1) return Core::Err("the archive is larger than 4 GB");
        in.seekg(0);
        std::vector<uint8_t> bytes; bytes.resize(size_t(size));
        in.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!in) return Core::Err("cannot read '" + path + "'");
        return OpenMemory(std::move(bytes), limits);
    }

    Core::Result<ZipArchive> ZipArchive::OpenMemory(std::vector<uint8_t> bytes, const ArchiveLimits& limits)
    {
        if (bytes.size() < 22) return Core::Err("not a zip archive");
        // End of central directory : the last 22 bytes, or before a comment of up to 65535 bytes
        size_t eocd = SIZE_MAX;
        const size_t lowest = bytes.size() > 22 + 65535 ? bytes.size() - 22 - 65535 : 0;
        for (size_t i = bytes.size() - 22 + 1; i-- > lowest;) {
            if (U32(&bytes[i]) == 0x06054b50u) { eocd = i; break; }
        }
        if (eocd == SIZE_MAX) return Core::Err("not a zip archive (no end of central directory)");
        const uint16_t disk = U16(&bytes[eocd + 4]);
        const uint16_t entries = U16(&bytes[eocd + 10]);
        const uint32_t cdSize = U32(&bytes[eocd + 12]);
        const uint32_t cdOffset = U32(&bytes[eocd + 16]);
        if (disk != 0 || U16(&bytes[eocd + 6]) != 0) return Core::Err("multi-disk archives are not supported");
        if (entries == 0xFFFF || cdOffset == 0xFFFFFFFFu) return Core::Err("zip64 archives are not supported");
        if (entries > limits.maxEntries) return Core::Err("the archive has too many entries (" + std::to_string(entries) + ")");
        if (uint64_t(cdOffset) + cdSize > bytes.size()) return Core::Err("the central directory is out of the file");

        ZipArchive zip;
        zip.m_Limits = limits;
        size_t at = cdOffset;
        uint64_t total = 0;
        for (uint16_t i = 0; i < entries; ++i) {
            if (at + 46 > bytes.size() || U32(&bytes[at]) != 0x02014b50u) return Core::Err("corrupt central directory");
            const uint16_t flags = U16(&bytes[at + 8]);
            Entry e;
            e.method = U16(&bytes[at + 10]);
            e.crc = U32(&bytes[at + 16]);
            e.compressedSize = U32(&bytes[at + 20]);
            e.size = U32(&bytes[at + 24]);
            const uint16_t nameLen = U16(&bytes[at + 28]);
            const uint16_t extraLen = U16(&bytes[at + 30]);
            const uint16_t commentLen = U16(&bytes[at + 32]);
            e.localHeaderOffset = U32(&bytes[at + 42]);
            if (at + 46 + nameLen + extraLen + commentLen > bytes.size()) return Core::Err("corrupt central directory");
            e.path.assign(reinterpret_cast<const char*>(&bytes[at + 46]), nameLen);
            at += 46 + size_t(nameLen) + extraLen + commentLen;

            if (flags & 1) return Core::Err("'" + e.path + "' is encrypted");
            if (e.size == 0xFFFFFFFFu || e.compressedSize == 0xFFFFFFFFu) return Core::Err("zip64 entries are not supported");
            e.isDirectory = !e.path.empty() && e.path.back() == '/';
            std::string checked = e.isDirectory ? e.path.substr(0, e.path.size() - 1) : e.path;
            if (!IsSafeArchivePath(checked)) return Core::Err("the entry '" + e.path + "' would leave the folder it is extracted to");
            if (e.method != 0 && e.method != 8) return Core::Err("'" + e.path + "' uses compression method " + std::to_string(e.method) + " (stored and deflate only)");
            if (e.size > limits.maxEntrySize) return Core::Err("'" + e.path + "' is too large");
            if (e.compressedSize > 0 && e.size / e.compressedSize > limits.maxCompressionRatio) return Core::Err("'" + e.path + "' expands too much (a zip bomb ?)");
            if (e.compressedSize == 0 && e.size > 0 && !e.isDirectory) return Core::Err("'" + e.path + "' is inconsistent");
            total += e.size;
            if (total > limits.maxTotalSize) return Core::Err("the archive expands to more than the allowed size");
            for (const Entry& other : zip.m_Entries)
                if (other.path == e.path) return Core::Err("the entry '" + e.path + "' appears twice");
            if (!e.isDirectory) e.path = checked;
            else e.path = checked;
            zip.m_Entries.push_back(std::move(e));
        }
        zip.m_Bytes = std::move(bytes);
        return Core::Ok(std::move(zip));
    }

    const ZipArchive::Entry* ZipArchive::Find(const std::string& path) const
    {
        for (const Entry& e : m_Entries) if (e.path == path) return &e;
        return nullptr;
    }

    Core::Result<std::vector<uint8_t>> ZipArchive::Read(const std::string& path) const
    {
        const Entry* e = Find(path);
        if (!e) return Core::Err("'" + path + "' is not in the archive");
        return Read(*e);
    }

    Core::Result<std::vector<uint8_t>> ZipArchive::Read(const Entry& e) const
    {
        const size_t at = e.localHeaderOffset;
        if (at + 30 > m_Bytes.size() || U32(&m_Bytes[at]) != 0x04034b50u) return Core::Err("'" + e.path + "' : corrupt local header");
        const size_t dataAt = at + 30 + U16(&m_Bytes[at + 26]) + U16(&m_Bytes[at + 28]);
        if (dataAt + e.compressedSize > m_Bytes.size()) return Core::Err("'" + e.path + "' : data out of the file");
        std::vector<uint8_t> out;
        if (e.method == 0) {
            if (e.compressedSize != e.size) return Core::Err("'" + e.path + "' : inconsistent sizes");
            out.assign(m_Bytes.begin() + long(dataAt), m_Bytes.begin() + long(dataAt + e.size));
        } else {
            auto inflated = Inflate(m_Bytes.data() + dataAt, size_t(e.compressedSize), size_t(e.size));     // dataAt may be the end (no data)
            if (!inflated) return Core::Err("'" + e.path + "' : " + inflated.GetError().message);
            out = std::move(inflated.Value());
            if (out.size() != e.size) return Core::Err("'" + e.path + "' : its size is not what the archive says");
        }
        if (Crc32(out.data(), out.size()) != e.crc) return Core::Err("'" + e.path + "' : checksum mismatch");
        return Core::Ok(std::move(out));
    }

    Core::Result<size_t> ZipArchive::ExtractAll(const std::string& directory) const
    {
        std::error_code ec;
        fs::create_directories(directory, ec);
        if (ec) return Core::Err("cannot create '" + directory + "'");
        const fs::path root = fs::weakly_canonical(directory, ec);
        size_t written = 0;
        for (const Entry& e : m_Entries) {
            const fs::path target = root / e.path;
            // Belt and braces : the paths were checked at Open, the result is checked again
            const std::string rel = fs::relative(fs::weakly_canonical(target, ec), root, ec).generic_string();
            if (rel.compare(0, 2, "..") == 0) return Core::Err("'" + e.path + "' would leave the folder");
            if (e.isDirectory) { fs::create_directories(target, ec); continue; }
            auto bytes = Read(e);
            if (!bytes) return Core::Err(bytes.GetError().message);
            fs::create_directories(target.parent_path(), ec);
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) return Core::Err("cannot write '" + target.string() + "'");
            out.write(reinterpret_cast<const char*>(bytes.Value().data()), std::streamsize(bytes.Value().size()));
            if (!out) return Core::Err("cannot write '" + target.string() + "'");
            ++written;
        }
        return Core::Ok(written);
    }

    // ---- ZipWriter ----

    void ZipWriter::Add(const std::string& path, const void* data, size_t size)
    {
        Item item;
        item.path = path;
        item.data.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
        m_Items.push_back(std::move(item));
    }

    Core::Result<size_t> ZipWriter::AddDirectory(const std::string& directory, const std::string& prefix, const std::vector<std::string>& excludeTopLevel)
    {
        std::error_code ec;
        if (!fs::is_directory(directory, ec)) return Core::Err("'" + directory + "' is not a folder");
        std::vector<std::string> files;
        for (fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            files.push_back(fs::relative(it->path(), directory, ec).generic_string());
        }
        std::sort(files.begin(), files.end());
        size_t added = 0;
        for (const std::string& rel : files) {
            if (std::find(excludeTopLevel.begin(), excludeTopLevel.end(), rel) != excludeTopLevel.end()) continue;
            std::ifstream in(fs::path(directory) / rel, std::ios::binary);
            if (!in) return Core::Err("cannot read '" + rel + "'");
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            Add(prefix + rel, bytes.data(), bytes.size());
            ++added;
        }
        return Core::Ok(added);
    }

    std::vector<uint8_t> ZipWriter::Finish() const
    {
        std::vector<uint8_t> out;
        auto put16 = [&](uint16_t v) { out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); };
        auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i))); };
        struct Central { uint32_t crc, size, offset; };
        std::vector<Central> central;
        for (const Item& item : m_Items) {
            const uint32_t crc = Crc32(item.data.data(), item.data.size());
            central.push_back({ crc, uint32_t(item.data.size()), uint32_t(out.size()) });
            put32(0x04034b50u); put16(20); put16(0x0800); put16(0); put16(0); put16(0x21);
            put32(crc); put32(uint32_t(item.data.size())); put32(uint32_t(item.data.size()));
            put16(uint16_t(item.path.size())); put16(0);
            out.insert(out.end(), item.path.begin(), item.path.end());
            out.insert(out.end(), item.data.begin(), item.data.end());
        }
        const uint32_t cdStart = uint32_t(out.size());
        for (size_t i = 0; i < m_Items.size(); ++i) {
            const Item& item = m_Items[i];
            put32(0x02014b50u); put16(20); put16(20); put16(0x0800); put16(0); put16(0); put16(0x21);
            put32(central[i].crc); put32(central[i].size); put32(central[i].size);
            put16(uint16_t(item.path.size())); put16(0); put16(0); put16(0); put16(0); put32(0);
            put32(central[i].offset);
            out.insert(out.end(), item.path.begin(), item.path.end());
        }
        const uint32_t cdSize = uint32_t(out.size()) - cdStart;
        put32(0x06054b50u); put16(0); put16(0); put16(uint16_t(m_Items.size())); put16(uint16_t(m_Items.size()));
        put32(cdSize); put32(cdStart); put16(0);
        return out;
    }

    Core::Result<bool> ZipWriter::Save(const std::string& path) const
    {
        std::error_code ec;
        fs::create_directories(fs::path(path).parent_path(), ec);
        const std::vector<uint8_t> bytes = Finish();
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return Core::Err("cannot write '" + path + "'");
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!out) return Core::Err("cannot write '" + path + "'");
        return Core::Ok(true);
    }
}
