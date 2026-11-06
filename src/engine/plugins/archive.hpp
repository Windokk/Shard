#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/containers/result.hpp"

namespace Shard::Engine::Plugins {

    uint32_t Crc32(const void* data, size_t size, uint32_t previous = 0);

    Core::Result<std::vector<uint8_t>> Inflate(const uint8_t* data, size_t size, size_t maxOutput);

    struct ArchiveLimits {
        size_t maxEntries = 20000;
        uint64_t maxEntrySize = 512ull * 1024 * 1024;
        uint64_t maxTotalSize = 2048ull * 1024 * 1024;
        uint32_t maxCompressionRatio = 1000;
    };

    class ZipArchive {
    public:
        struct Entry {
            std::string path;
            uint64_t compressedSize = 0;
            uint64_t size = 0;
            uint32_t crc = 0;
            uint16_t method = 0;
            uint32_t localHeaderOffset = 0;
            bool isDirectory = false;
        };

        static Core::Result<ZipArchive> Open(const std::string& path, const ArchiveLimits& limits = {});
        static Core::Result<ZipArchive> OpenMemory(std::vector<uint8_t> bytes, const ArchiveLimits& limits = {});

        const std::vector<Entry>& Entries() const { return m_Entries; }
        const Entry* Find(const std::string& path) const;

        Core::Result<std::vector<uint8_t>> Read(const Entry& entry) const;
        Core::Result<std::vector<uint8_t>> Read(const std::string& path) const;

        Core::Result<size_t> ExtractAll(const std::string& directory) const;

    private:
        std::vector<uint8_t> m_Bytes;
        std::vector<Entry> m_Entries;
        ArchiveLimits m_Limits;
    };

    class ZipWriter {
    public:
        void Add(const std::string& path, const void* data, size_t size);
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

    bool IsSafeArchivePath(const std::string& path);
}
