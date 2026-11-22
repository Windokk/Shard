#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "engine/core/collision/collision_types.hpp"

namespace Shard::Engine::Collision {

    struct TrackerEvent {
        EventKind kind = EventKind::OverlapBegin;
        uint64_t a = 0;
        uint64_t b = 0;
        EndCause cause = EndCause::None;
    };

    class OverlapTracker {
    public:
        void Add(uint64_t a, uint64_t b, uint64_t subA, uint64_t subB, std::vector<TrackerEvent>& out);
        void Remove(uint64_t a, uint64_t b, uint64_t subA, uint64_t subB, std::vector<TrackerEvent>& out);
        void RemoveCollider(uint64_t id, EndCause cause, std::vector<TrackerEvent>& out);
        void RemovePair(uint64_t a, uint64_t b, EndCause cause, std::vector<TrackerEvent>& out);

        bool Overlaps(uint64_t a, uint64_t b) const;
        std::vector<uint64_t> Overlapping(uint64_t id) const;
        size_t PairCount() const { return m_Pairs.size(); }
        bool Tracks(uint64_t id) const { return m_Adjacent.count(id) != 0; }
        void Clear();

    private:
        using PairKey = std::pair<uint64_t, uint64_t>;
        using SubKey = std::pair<uint64_t, uint64_t>;

        void Link(uint64_t a, uint64_t b);
        void Unlink(uint64_t a, uint64_t b);

        std::map<PairKey, std::set<SubKey>> m_Pairs;
        std::map<uint64_t, std::set<uint64_t>> m_Adjacent;
    };
}
