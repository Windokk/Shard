#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Shard::Engine::Core {

    /// @brief Records the allocations of the engine's allocators (and of anything that calls it) by tag, to
    /// show what is still alive at shutdown and how much each system uses.
    ///
    /// It is a debugging aid : off by default in release builds (a recorded allocation costs a mutex and a hash
    /// map insert), on by default in debug builds, switchable at run time with SetEnabled or the `memory.track`
    /// cvar. Allocations made while it is off are not seen, and freeing something it never saw is ignored.
    class MemoryTracker {
    public:
        static MemoryTracker& Get();

        void SetEnabled(bool enabled) { m_Enabled.store(enabled, std::memory_order_relaxed); }
        bool IsEnabled() const { return m_Enabled.load(std::memory_order_relaxed); }

        /// @param tag static or long-lived text naming the owner ("FrameAllocator", "Physics"...). Copied.
        void RecordAlloc(const void* ptr, size_t size, const char* tag, const char* file = nullptr, int line = 0);
        void RecordFree(const void* ptr);

        struct TagStats {
            std::string tag;
            size_t liveBytes = 0;
            size_t liveCount = 0;
            size_t peakBytes = 0;
            uint64_t totalAllocations = 0;
        };
        struct Leak {
            const void* ptr = nullptr;
            size_t size = 0;
            std::string tag;
            std::string file;
            int line = 0;
            uint64_t id = 0;            ///< allocation order, 1 = first
        };

        std::vector<TagStats> GetStats() const;       ///< sorted by tag
        std::vector<Leak> GetLiveAllocations() const; ///< sorted by allocation order
        size_t LiveCount() const;
        size_t LiveBytes() const;

        /// @brief Human readable list of what is still allocated (empty string when nothing is), grouped by tag.
        std::string Report(size_t maxLines = 32) const;

        /// @brief Forgets everything (tests).
        void Reset();

    private:
        struct Record {
            size_t size;
            std::string tag;
            std::string file;
            int line;
            uint64_t id;
        };
        struct TagCounters {
            size_t liveBytes = 0, liveCount = 0, peakBytes = 0;
            uint64_t totalAllocations = 0;
        };

        std::atomic<bool> m_Enabled;
        mutable std::mutex m_Mutex;
        std::unordered_map<const void*, Record> m_Live;
        std::unordered_map<std::string, TagCounters> m_Tags;
        uint64_t m_NextId = 1;

        MemoryTracker();
    };

    /// Convenience : record with the call site.
    #define SHARD_TRACK_ALLOC(ptr, size, tag) ::Shard::Engine::Core::MemoryTracker::Get().RecordAlloc((ptr), (size), (tag), __FILE__, __LINE__)
    #define SHARD_TRACK_FREE(ptr) ::Shard::Engine::Core::MemoryTracker::Get().RecordFree(ptr)
}
