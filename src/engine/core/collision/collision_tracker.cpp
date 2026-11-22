#include "collision_tracker.hpp"

namespace Shard::Engine::Collision {

    void OverlapTracker::Link(uint64_t a, uint64_t b)
    {
        m_Adjacent[a].insert(b);
        m_Adjacent[b].insert(a);
    }

    void OverlapTracker::Unlink(uint64_t a, uint64_t b)
    {
        for (auto [x, y] : { std::pair{ a, b }, std::pair{ b, a } }) {
            auto it = m_Adjacent.find(x);
            if (it == m_Adjacent.end())
                continue;
            it->second.erase(y);
            if (it->second.empty())
                m_Adjacent.erase(it);
        }
    }

    void OverlapTracker::Add(uint64_t a, uint64_t b, uint64_t subA, uint64_t subB, std::vector<TrackerEvent>& out)
    {
        if (a > b) {
            std::swap(a, b);
            std::swap(subA, subB);
        }
        auto& subs = m_Pairs[{ a, b }];
        const bool first = subs.empty();
        subs.insert({ subA, subB });
        if (first) {
            Link(a, b);
            out.push_back({ EventKind::OverlapBegin, a, b, EndCause::None });
        }
    }

    void OverlapTracker::Remove(uint64_t a, uint64_t b, uint64_t subA, uint64_t subB, std::vector<TrackerEvent>& out)
    {
        if (a > b) {
            std::swap(a, b);
            std::swap(subA, subB);
        }
        auto it = m_Pairs.find({ a, b });
        if (it == m_Pairs.end())
            return;
        it->second.erase({ subA, subB });
        if (it->second.empty()) {
            m_Pairs.erase(it);
            Unlink(a, b);
            out.push_back({ EventKind::OverlapEnd, a, b, EndCause::Separated });
        }
    }

    void OverlapTracker::RemovePair(uint64_t a, uint64_t b, EndCause cause, std::vector<TrackerEvent>& out)
    {
        if (a > b)
            std::swap(a, b);
        auto it = m_Pairs.find({ a, b });
        if (it == m_Pairs.end())
            return;
        m_Pairs.erase(it);
        Unlink(a, b);
        out.push_back({ EventKind::OverlapEnd, a, b, cause });
    }

    void OverlapTracker::RemoveCollider(uint64_t id, EndCause cause, std::vector<TrackerEvent>& out)
    {
        auto it = m_Adjacent.find(id);
        if (it == m_Adjacent.end())
            return;
        const std::vector<uint64_t> others(it->second.begin(), it->second.end());
        for (uint64_t other : others)
            RemovePair(id, other, cause, out);
    }

    bool OverlapTracker::Overlaps(uint64_t a, uint64_t b) const
    {
        if (a > b)
            std::swap(a, b);
        return m_Pairs.count({ a, b }) != 0;
    }

    std::vector<uint64_t> OverlapTracker::Overlapping(uint64_t id) const
    {
        auto it = m_Adjacent.find(id);
        if (it == m_Adjacent.end())
            return {};
        return std::vector<uint64_t>(it->second.begin(), it->second.end());
    }

    void OverlapTracker::Clear()
    {
        m_Pairs.clear();
        m_Adjacent.clear();
    }
}
