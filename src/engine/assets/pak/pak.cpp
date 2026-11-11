#include "engine/assets/pak/pak.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace Shard::Engine::Pak {

    namespace fs = std::filesystem;

    namespace {
        constexpr size_t kHeaderSize = 32;
        constexpr char kMagic[4] = { 'S', 'H', 'P', 'K' };

        const std::array<uint32_t, 256>& CrcTable()
        {
            static const std::array<uint32_t, 256> table = [] {
                std::array<uint32_t, 256> t{};
                for (uint32_t i = 0; i < 256; ++i) {
                    uint32_t c = i;
                    for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                    t[i] = c;
                }
                return t;
            }();
            return table;
        }

        void PutU16(std::string& out, uint16_t v)
        {
            out.push_back(char(v & 0xFF));
            out.push_back(char((v >> 8) & 0xFF));
        }
        void PutU32(std::string& out, uint32_t v)
        {
            for (int i = 0; i < 4; ++i) out.push_back(char((v >> (8 * i)) & 0xFF));
        }
        void PutU64(std::string& out, uint64_t v)
        {
            for (int i = 0; i < 8; ++i) out.push_back(char((v >> (8 * i)) & 0xFF));
        }

        uint64_t GetLE(const unsigned char* p, int bytes)
        {
            uint64_t v = 0;
            for (int i = 0; i < bytes; ++i) v |= uint64_t(p[i]) << (8 * i);
            return v;
        }

        bool Fail(std::string* error, const std::string& message)
        {
            if (error) *error = message;
            return false;
        }
    }

    uint32_t Crc32(const void* data, size_t size)
    {
        const auto& table = CrcTable();
        const auto* p = static_cast<const unsigned char*>(data);
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < size; ++i) c = table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    }

    std::string Compress(const std::string& in)
    {
        const size_t n = in.size();
        const auto* s = reinterpret_cast<const unsigned char*>(in.data());
        std::string out;
        out.reserve(n / 2 + 16);

        auto hash = [&](size_t p) {
            uint32_t v;
            std::memcpy(&v, s + p, 4);
            return size_t((v * 2654435761u) >> 16);
        };
        auto putLength = [&](size_t len) {
            while (len >= 255) { out.push_back(char(255)); len -= 255; }
            out.push_back(char(len));
        };
        auto emit = [&](size_t litStart, size_t litLen, size_t matchLen, size_t offset) {
            const size_t ml = matchLen ? matchLen - 4 : 0;
            unsigned token = unsigned(litLen >= 15 ? 15 : litLen) << 4;
            if (matchLen) token |= unsigned(ml >= 15 ? 15 : ml);
            out.push_back(char(token));
            if (litLen >= 15) putLength(litLen - 15);
            out.append(in, litStart, litLen);
            if (matchLen) {
                out.push_back(char(offset & 0xFF));
                out.push_back(char((offset >> 8) & 0xFF));
                if (ml >= 15) putLength(ml - 15);
            }
        };

        size_t anchor = 0;
        if (n >= 13) {
            std::vector<int64_t> table(size_t(1) << 16, -1);
            const size_t limit = n - 12;
            size_t i = 0;
            while (i < limit) {
                const size_t h = hash(i);
                const int64_t candidate = table[h];
                table[h] = int64_t(i);
                if (candidate >= 0 && i - size_t(candidate) <= 65535 && std::memcmp(s + candidate, s + i, 4) == 0) {
                    const size_t from = size_t(candidate);
                    const size_t maxLen = n - 5 - i;
                    size_t len = 4;
                    while (len < maxLen && s[from + len] == s[i + len]) ++len;
                    emit(anchor, i - anchor, len, i - from);
                    i += len;
                    anchor = i;
                } else {
                    ++i;
                }
            }
        }
        emit(anchor, n - anchor, 0, 0);
        return out;
    }

    std::optional<std::string> Decompress(const char* data, size_t size, size_t originalSize)
    {
        const auto* p = reinterpret_cast<const unsigned char*>(data);
        std::string out;
        out.reserve(originalSize);
        size_t ip = 0;
        auto readLength = [&](size_t& len) {
            unsigned char b;
            do {
                if (ip >= size) return false;
                b = p[ip++];
                len += b;
            } while (b == 255);
            return true;
        };
        while (ip < size) {
            const unsigned token = p[ip++];
            size_t lit = token >> 4;
            if (lit == 15 && !readLength(lit)) return std::nullopt;
            if (lit > size - ip || out.size() + lit > originalSize) return std::nullopt;
            out.append(data + ip, lit);
            ip += lit;
            if (ip == size) break; // the last sequence has no match
            if (size - ip < 2) return std::nullopt;
            const size_t offset = size_t(p[ip]) | (size_t(p[ip + 1]) << 8);
            ip += 2;
            size_t ml = token & 15u;
            if (ml == 15 && !readLength(ml)) return std::nullopt;
            ml += 4;
            if (offset == 0 || offset > out.size() || out.size() + ml > originalSize) return std::nullopt;
            const size_t from = out.size() - offset;
            for (size_t k = 0; k < ml; ++k) out.push_back(out[from + k]);
        }
        if (out.size() != originalSize) return std::nullopt;
        return out;
    }

    std::string NormalizePath(const std::string& path)
    {
        std::string s = path;
        std::replace(s.begin(), s.end(), '\\', '/');
        while (!s.empty() && s.front() == '/') s.erase(0, 1);
        return s;
    }

    bool IsSafePath(const std::string& path)
    {
        if (path.empty() || path.size() > 0xFFFF) return false;
        if (path.front() == '/' || path.find(':') != std::string::npos || path.find('\\') != std::string::npos) return false;
        std::stringstream ss(path);
        std::string part;
        while (std::getline(ss, part, '/'))
            if (part == ".." || part.empty()) return false;
        return true;
    }

    bool PakWriter::Add(const std::string& path, std::string bytes)
    {
        const std::string p = NormalizePath(path);
        if (!IsSafePath(p)) return false;
        Source source;
        source.size = bytes.size();
        source.bytes = std::move(bytes);
        m_Files[p] = std::move(source);
        return true;
    }

    // Only the path is kept : the bytes are read when the pak is written, one file at a time, so a project of several GB does not sit in memory
    bool PakWriter::AddFile(const std::string& path, const std::string& diskFile)
    {
        const std::string p = NormalizePath(path);
        if (!IsSafePath(p)) return false;
        std::error_code ec;
        if (!fs::is_regular_file(diskFile, ec)) return false;
        Source source;
        source.size = fs::file_size(diskFile, ec);
        if (ec) return false;
        source.diskFile = diskFile;
        m_Files[p] = std::move(source);
        return true;
    }

    size_t PakWriter::AddFolder(const std::string& folder, const std::string& prefix, std::vector<std::string>* skipped)
    {
        std::error_code ec;
        if (!fs::is_directory(folder, ec)) return 0;
        size_t added = 0;
        for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            std::string rel = fs::relative(it->path(), folder, ec).generic_string();
            if (!prefix.empty()) rel = NormalizePath(prefix) + "/" + rel;
            if (AddFile(rel, it->path().string())) ++added;
            else if (skipped) skipped->push_back(rel);
        }
        return added;
    }

    uint64_t PakWriter::OriginalBytes() const
    {
        uint64_t total = 0;
        for (const auto& f : m_Files) total += f.second.size;
        return total;
    }

    namespace {
        // Already compressed data (png, jpg, ogg, zip...) does not shrink : a sample tells, so big files are not compressed for nothing
        bool WorthCompressing(const std::string& bytes)
        {
            if (bytes.size() < 64) return false;
            constexpr size_t kSample = 65536;
            if (bytes.size() <= 4 * kSample) return true;
            const std::string sample = Compress(bytes.substr(0, kSample));
            return sample.size() < kSample - kSample / 10;
        }
    }

    bool PakWriter::WriteTo(std::ostream& out, std::string* error) const
    {
        char blank[kHeaderSize] = {};
        out.write(blank, kHeaderSize);
        uint64_t at = kHeaderSize;
        std::string toc;
        for (const auto& [path, source] : m_Files) {
            std::string bytes;
            if (source.diskFile.empty()) {
                bytes = source.bytes;
            } else {
                std::ifstream in(source.diskFile, std::ios::binary);
                if (!in) return Fail(error, "cannot read " + source.diskFile);
                std::stringstream ss;
                ss << in.rdbuf();
                bytes = ss.str();
                if (bytes.size() != source.size) return Fail(error, source.diskFile + " changed while it was packed");
            }
            std::string packed;
            bool compressed = false;
            if (m_Compress && WorthCompressing(bytes)) {
                packed = Compress(bytes);
                compressed = packed.size() < bytes.size();
            }
            const std::string& stored = compressed ? packed : bytes;
            PutU16(toc, uint16_t(path.size()));
            toc += path;
            PutU64(toc, at);
            PutU64(toc, stored.size());
            PutU64(toc, bytes.size());
            PutU32(toc, Crc32(bytes.data(), bytes.size()));
            PutU32(toc, compressed ? 1u : 0u);
            out.write(stored.data(), std::streamsize(stored.size()));
            at += stored.size();
        }
        out.write(toc.data(), std::streamsize(toc.size()));

        std::string header;
        header.append(kMagic, 4);
        PutU32(header, kVersion);
        PutU64(header, at);
        PutU64(header, toc.size());
        PutU32(header, uint32_t(m_Files.size()));
        PutU32(header, 0);
        out.seekp(0);
        out.write(header.data(), std::streamsize(header.size()));
        out.seekp(0, std::ios::end);
        return out.good() ? true : Fail(error, "cannot write the pak");
    }

    std::string PakWriter::ToBytes() const
    {
        std::ostringstream out(std::ios::binary);
        if (!WriteTo(out)) return {};
        return out.str();
    }

    bool PakWriter::Write(const std::string& pakFile, std::string* error) const
    {
        std::error_code ec;
        const fs::path target(pakFile);
        if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
        // Written next to the target then renamed, so a crash never leaves half a pak
        const std::string temp = pakFile + ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) return Fail(error, "cannot create " + temp);
            if (!WriteTo(out, error)) {
                out.close();
                fs::remove(temp, ec);
                return false;
            }
        }
        fs::rename(temp, target, ec);
        if (ec) {
            fs::remove(target, ec);
            std::error_code ec2;
            fs::rename(temp, target, ec2);
            if (ec2) return Fail(error, "cannot move " + temp + " to " + pakFile);
        }
        return true;
    }

    bool PakReader::Open(const std::string& pakFile, std::string* error)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Open = false;
        m_Entries.clear();
        m_Index.clear();
        m_File = pakFile;

        std::ifstream in(pakFile, std::ios::binary);
        if (!in) return Fail(error, "cannot open " + pakFile);
        in.seekg(0, std::ios::end);
        const auto fileSize = uint64_t(in.tellg());
        if (fileSize < kHeaderSize) return Fail(error, "not a pak (too small)");
        in.seekg(0);

        unsigned char header[kHeaderSize];
        in.read(reinterpret_cast<char*>(header), kHeaderSize);
        if (!in || std::string(reinterpret_cast<char*>(header), 4) != std::string(kMagic, 4)) return Fail(error, "not a pak (bad magic)");
        const uint32_t version = uint32_t(GetLE(header + 4, 4));
        if (version != kVersion) return Fail(error, "unsupported pak version " + std::to_string(version));
        const uint64_t tocOffset = GetLE(header + 8, 8);
        const uint64_t tocSize = GetLE(header + 16, 8);
        const uint32_t count = uint32_t(GetLE(header + 24, 4));
        if (tocOffset < kHeaderSize || tocOffset > fileSize || tocSize > fileSize - tocOffset) return Fail(error, "pak table of contents is out of range");

        std::string toc(size_t(tocSize), '\0');
        in.seekg(std::streamoff(tocOffset));
        in.read(toc.data(), std::streamsize(tocSize));
        if (!in) return Fail(error, "cannot read the table of contents");

        const auto* p = reinterpret_cast<const unsigned char*>(toc.data());
        size_t at = 0;
        for (uint32_t i = 0; i < count; ++i) {
            if (at + 2 > toc.size()) return Fail(error, "pak table of contents is truncated");
            const size_t len = size_t(GetLE(p + at, 2));
            at += 2;
            if (at + len + 32 > toc.size()) return Fail(error, "pak table of contents is truncated");
            Entry e;
            e.path.assign(toc, at, len);
            at += len;
            e.offset = GetLE(p + at, 8);
            e.storedSize = GetLE(p + at + 8, 8);
            e.size = GetLE(p + at + 16, 8);
            e.crc = uint32_t(GetLE(p + at + 24, 4));
            e.compressed = (GetLE(p + at + 28, 4) & 1u) != 0;
            at += 32;
            if (!IsSafePath(e.path)) return Fail(error, "pak holds an unsafe path '" + e.path + "'");
            if (e.offset < kHeaderSize || e.offset > tocOffset || e.storedSize > tocOffset - e.offset) return Fail(error, "pak entry '" + e.path + "' is out of range");
            if (!e.compressed && e.size != e.storedSize) return Fail(error, "pak entry '" + e.path + "' has inconsistent sizes");
            if (e.size > (uint64_t(1) << 32)) return Fail(error, "pak entry '" + e.path + "' is too large");
            m_Index[e.path] = m_Entries.size();
            m_Entries.push_back(std::move(e));
        }
        m_Open = true;
        return true;
    }

    const Entry* PakReader::Find(const std::string& path) const
    {
        const auto it = m_Index.find(NormalizePath(path));
        return it == m_Index.end() ? nullptr : &m_Entries[it->second];
    }

    bool PakReader::Has(const std::string& path) const { return Find(path) != nullptr; }

    std::optional<std::string> PakReader::Read(const std::string& path) const
    {
        const Entry* e = Find(path);
        if (!e) return std::nullopt;
        std::string bytes(size_t(e->storedSize), '\0');
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            std::ifstream in(m_File, std::ios::binary);
            if (!in) return std::nullopt;
            in.seekg(std::streamoff(e->offset));
            if (!bytes.empty()) in.read(bytes.data(), std::streamsize(bytes.size()));
            if (!in) return std::nullopt;
        }
        if (e->compressed) {
            auto plain = Decompress(bytes.data(), bytes.size(), size_t(e->size));
            if (!plain) return std::nullopt;
            bytes = std::move(*plain);
        }
        if (Crc32(bytes.data(), bytes.size()) != e->crc) return std::nullopt;
        return bytes;
    }

    std::vector<std::string> PakReader::List(const std::string& subdir) const
    {
        std::string prefix = NormalizePath(subdir);
        while (!prefix.empty() && prefix.back() == '/') prefix.pop_back();
        if (!prefix.empty()) prefix += '/';
        std::vector<std::string> out;
        for (const Entry& e : m_Entries)
            if (e.path.compare(0, prefix.size(), prefix) == 0) out.push_back(e.path);
        std::sort(out.begin(), out.end());
        return out;
    }

    bool Unpack(const PakReader& pak, const std::string& folder, std::string* error)
    {
        for (const Entry& e : pak.Entries()) {
            auto bytes = pak.Read(e.path);
            if (!bytes) return Fail(error, "cannot read '" + e.path + "' (damaged pak ?)");
            const fs::path target = fs::path(folder) / e.path;
            std::error_code ec;
            fs::create_directories(target.parent_path(), ec);
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) return Fail(error, "cannot create " + target.string());
            out.write(bytes->data(), std::streamsize(bytes->size()));
            out.close();
            if (!out) return Fail(error, "cannot write " + target.string());
        }
        return true;
    }

    std::string UnpackCached(const std::string& pakFile, const std::string& cacheRoot, std::string* error)
    {
        PakReader pak;
        if (!pak.Open(pakFile, error)) return {};
        // The key follows the table of contents (paths, sizes, crcs), so a rebuilt pak gets a fresh folder
        std::string key;
        for (const Entry& e : pak.Entries()) key += e.path + ":" + std::to_string(e.size) + ":" + std::to_string(e.crc) + ";";
        char hash[16];
        std::snprintf(hash, sizeof(hash), "%08x", Crc32(key.data(), key.size()));
        const fs::path folder = fs::path(cacheRoot) / (fs::path(pakFile).stem().string() + "-" + hash);
        const fs::path done = folder / ".shardpak-complete";
        std::error_code ec;
        if (fs::is_regular_file(done, ec)) return folder.generic_string();
        fs::remove_all(folder, ec);
        fs::create_directories(folder, ec);
        if (!Unpack(pak, folder.string(), error)) return {};
        std::ofstream(done).put('1');
        return folder.generic_string();
    }

    std::vector<std::string> PakReader::Verify() const
    {
        std::vector<std::string> bad;
        for (const Entry& e : m_Entries)
            if (!Read(e.path)) bad.push_back(e.path);
        return bad;
    }
}
