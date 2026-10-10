#include "engine/core/memory/memory_tracker.hpp"

#include <algorithm>
#include <map>
#include <sstream>

#include "engine/core/config/cvar.hpp"

namespace Shard::Engine::Core {

    namespace {
#ifdef NDEBUG
        constexpr bool kTrackByDefault = false;
#else
        constexpr bool kTrackByDefault = true;
#endif
        // `memory.track 1` in the console (or a config file) switches the tracker on or off
        CVar<bool> cvTrack("memory.track", kTrackByDefault, "Record allocations of the engine's allocators (leak report at shutdown)",
                           CVarFlags::None);
        const bool g_Wired = [] {
            cvTrack.AddOnChange([](const CVarBase&) { MemoryTracker::Get().SetEnabled(cvTrack.Get()); });
            return true;
        }();
    }

    MemoryTracker::MemoryTracker() {
#ifdef NDEBUG
        m_Enabled.store(false, std::memory_order_relaxed);
#else
        m_Enabled.store(true, std::memory_order_relaxed);
#endif
    }

    MemoryTracker& MemoryTracker::Get() {
        static MemoryTracker instance;
        return instance;
    }

    void MemoryTracker::RecordAlloc(const void* ptr, size_t size, const char* tag, const char* file, int line) {
        if (!IsEnabled() || !ptr) return;
        std::lock_guard<std::mutex> lock(m_Mutex);

        Record record{size, tag ? tag : "untagged", file ? file : "", line, m_NextId++};
        TagCounters& counters = m_Tags[record.tag];
        counters.liveBytes += size;
        counters.liveCount += 1;
        counters.peakBytes = std::max(counters.peakBytes, counters.liveBytes);
        counters.totalAllocations += 1;

        m_Live[ptr] = std::move(record);        // a reused address replaces the stale record
    }

    void MemoryTracker::RecordFree(const void* ptr) {
        if (!ptr) return;
        std::lock_guard<std::mutex> lock(m_Mutex);       // not gated on IsEnabled : a free must match its alloc
        auto it = m_Live.find(ptr);
        if (it == m_Live.end()) return;                   // allocated while tracking was off

        TagCounters& counters = m_Tags[it->second.tag];
        counters.liveBytes -= std::min(counters.liveBytes, it->second.size);
        counters.liveCount -= std::min<size_t>(counters.liveCount, 1);
        m_Live.erase(it);
    }

    std::vector<MemoryTracker::TagStats> MemoryTracker::GetStats() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::vector<TagStats> out;
        out.reserve(m_Tags.size());
        for (const auto& [tag, c] : m_Tags)
            out.push_back(TagStats{tag, c.liveBytes, c.liveCount, c.peakBytes, c.totalAllocations});
        std::sort(out.begin(), out.end(), [](const TagStats& a, const TagStats& b) { return a.tag < b.tag; });
        return out;
    }

    std::vector<MemoryTracker::Leak> MemoryTracker::GetLiveAllocations() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::vector<Leak> out;
        out.reserve(m_Live.size());
        for (const auto& [ptr, r] : m_Live)
            out.push_back(Leak{ptr, r.size, r.tag, r.file, r.line, r.id});
        std::sort(out.begin(), out.end(), [](const Leak& a, const Leak& b) { return a.id < b.id; });
        return out;
    }

    size_t MemoryTracker::LiveCount() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Live.size();
    }

    size_t MemoryTracker::LiveBytes() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        size_t total = 0;
        for (const auto& [ptr, r] : m_Live) total += r.size;
        return total;
    }

    std::string MemoryTracker::Report(size_t maxLines) const {
        const std::vector<Leak> live = GetLiveAllocations();
        if (live.empty()) return {};

        std::map<std::string, std::pair<size_t, size_t>> perTag;     // tag -> (count, bytes)
        for (const Leak& l : live) {
            auto& entry = perTag[l.tag];
            entry.first += 1;
            entry.second += l.size;
        }

        std::ostringstream out;
        out << live.size() << " allocation(s) still alive:\n";
        for (const auto& [tag, entry] : perTag)
            out << "  [" << tag << "] " << entry.first << " allocation(s), " << entry.second << " bytes\n";

        size_t shown = 0;
        for (const Leak& l : live) {
            if (shown++ >= maxLines) { out << "  ... " << (live.size() - maxLines) << " more\n"; break; }
            out << "  #" << l.id << " " << l.size << " bytes [" << l.tag << "]";
            if (!l.file.empty()) out << " allocated at " << l.file << ":" << l.line;
            out << "\n";
        }
        return out.str();
    }

    void MemoryTracker::Reset() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Live.clear();
        m_Tags.clear();
        m_NextId = 1;
    }
}
