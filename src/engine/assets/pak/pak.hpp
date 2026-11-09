#pragma once

#include <cstdint>
#include <fstream>
#include <ostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Shard::Engine::Pak {

    // .shardpak : one file holding many files, read by logical path.
    //   header (32 bytes) : "SHPK", u32 version, u64 tocOffset, u64 tocSize, u32 entryCount, u32 flags
    //   data              : the file bytes one after the other, as stored
    //   toc               : per entry : u16 pathLength, path, u64 offset, u64 storedSize, u64 size, u32 crc32, u32 flags (bit 0 : LZ compressed)
    // Everything is little endian. Entries are sorted by path, so the same input gives the same bytes.
    constexpr uint32_t kVersion = 2;
    constexpr const char* kExtension = ".shardpak";

    uint32_t Crc32(const void* data, size_t size);

    // LZ4-style block codec, no dependency. Decompress needs the original size and refuses anything inconsistent.
    std::string Compress(const std::string& in);
    std::optional<std::string> Decompress(const char* data, size_t size, size_t originalSize);

    struct Entry {
        std::string path;
        uint64_t offset = 0;
        uint64_t size = 0;        // original size
        uint64_t storedSize = 0;  // bytes in the pak
        uint32_t crc = 0;         // of the original bytes
        bool compressed = false;
    };

    class PakWriter {
    public:
        // Paths are stored with '/' and no leading slash. Returns false for an empty or unsafe path ("..", ':').
        bool Add(const std::string& path, std::string bytes);
        // Reads a file of the disk and adds it.
        bool AddFile(const std::string& path, const std::string& diskFile);
        // Adds every regular file of a folder (recursive) under an optional prefix. Returns the number added, skipped names go to `skipped`.
        size_t AddFolder(const std::string& folder, const std::string& prefix = {}, std::vector<std::string>* skipped = nullptr);

        // On by default. A file is stored raw when compressing does not make it smaller (already compressed formats).
        void SetCompression(bool on) { m_Compress = on; }
        size_t Count() const { return m_Files.size(); }
        uint64_t OriginalBytes() const;
        bool Write(const std::string& pakFile, std::string* error = nullptr) const;
        std::string ToBytes() const;

    private:
        struct Source {
            std::string bytes;     // when added from memory
            std::string diskFile;  // else read from here when written
            uint64_t size = 0;
        };
        bool WriteTo(std::ostream& out, std::string* error = nullptr) const;

        std::map<std::string, Source> m_Files;
        bool m_Compress = true;
    };

    class PakReader {
    public:
        // Reads the header and the table of contents. Files are read on demand.
        bool Open(const std::string& pakFile, std::string* error = nullptr);
        bool IsOpen() const { return m_Open; }
        const std::string& File() const { return m_File; }

        bool Has(const std::string& path) const;
        const Entry* Find(const std::string& path) const;
        // Checks the crc of the stored bytes.
        std::optional<std::string> Read(const std::string& path) const;
        // Paths under a subfolder ("" = all), sorted.
        std::vector<std::string> List(const std::string& subdir = {}) const;
        const std::vector<Entry>& Entries() const { return m_Entries; }

        // Checks every entry, returns the paths that fail.
        std::vector<std::string> Verify() const;

    private:
        std::string m_File;
        bool m_Open = false;
        std::vector<Entry> m_Entries;
        std::map<std::string, size_t> m_Index;
        mutable std::mutex m_Mutex;
    };

    // Writes every file of the pak under a folder. Returns false on the first failure.
    bool Unpack(const PakReader& pak, const std::string& folder, std::string* error = nullptr);
    // Unpacks into <cacheRoot>/<name>-<hash of the pak> once and returns that folder (reused while the pak is unchanged). Empty on failure.
    std::string UnpackCached(const std::string& pakFile, const std::string& cacheRoot, std::string* error = nullptr);

    std::string NormalizePath(const std::string& path);
    bool IsSafePath(const std::string& path);
}
