#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/containers/result.hpp"

namespace Shard::Engine::Plugins {

    uint32_t Crc32(const void* data, size_t size, uint32_t previous = 0);

    /// @brief Inflates a raw deflate stream (RFC 1951). Fails on a corrupt stream or when the output would pass `maxOutput`.
    Core::Result<std::vector<uint8_t>> Inflate(const uint8_t* data, size_t size, size_t maxOutput);

    /// Limits an archive is read within : a plugin archive comes from someone else
    struct ArchiveLimits {
        size_t maxEntries = 20000;
        uint64_t maxEntrySize = 512ull * 1024 * 1024;
        uint64_t maxTotalSize = 2048ull * 1024 * 1024;
        uint32_t maxCompressionRatio = 1000;        ///< an entry that expands by more than this is refused (a zip bomb)
    };

    /// @brief A ZIP file (stored and deflated entries, no ZIP64, no encryption), read for plugins : `.shardplugin` archives.
    class ZipArchive {
    public:
        struct Entry {
            std::string path;               ///< '/' separated, relative, no '..' (checked by Open)
            uint64_t compressedSize = 0;
            uint64_t size = 0;
            uint32_t crc = 0;
            uint16_t method = 0;
            uint32_t localHeaderOffset = 0;
            bool isDirectory = false;
        };

        /// @brief Reads the central directory. Refuses an entry whose path leaves the archive ("..", absolute, a drive, a backslash trick)
        /// and an archive past the limits.
        static Core::Result<ZipArchive> Open(const std::string& path, const ArchiveLimits& limits = {});
        static Core::Result<ZipArchive> OpenMemory(std::vector<uint8_t> bytes, const ArchiveLimits& limits = {});

        const std::vector<Entry>& Entries() const { return m_Entries; }
        const Entry* Find(const std::string& path) const;

        /// The bytes of an entry, its CRC checked
        Core::Result<std::vector<uint8_t>> Read(const Entry& entry) const;
        Core::Result<std::vector<uint8_t>> Read(const std::string& path) const;

        /// Writes every entry under `directory` (created). Nothing is written outside it.
        Core::Result<size_t> ExtractAll(const std::string& directory) const;

    private:
        std::vector<uint8_t> m_Bytes;
        std::vector<Entry> m_Entries;
        ArchiveLimits m_Limits;
    };

    /// @brief Writes a ZIP with stored (uncompressed) entries : the packager of plugin archives.
    class ZipWriter {
    public:
        void Add(const std::string& path, const void* data, size_t size);
        /// Adds a folder's files (recursively, sorted, so the same folder gives the same archive). `excludeTopLevel` names files at its root to leave out.
        Core::Result<size_t> AddDirectory(const std::string& directory, const std::string& prefix = {}, const std::vector<std::string>& excludeTopLevel = {});
        std::vector<uint8_t> Finish() const;
        Core::Result<bool> Save(const std::string& path) const;

    private:
        struct Item {
            std::string path;
            std::vector<uint8_t> data;
        };
        std::vector<Item> m_Items;
    };

    /// Whether `path` stays inside the folder it is joined to : relative, no "..", no drive letter, no backslash
    bool IsSafeArchivePath(const std::string& path);
}
