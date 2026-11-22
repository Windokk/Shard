#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/collision/collision_profile.hpp"

namespace Shard::Engine::Collision {

    struct Patch {
        std::optional<Mode> mode;
        std::optional<Response> defaultResponse;
        std::vector<Rule> rules;
        Core::TagContainer addIdentity;
    };

    struct OverrideEntry {
        uint64_t id = 0;
        std::string owner;
        int priority = 0;
        Patch patch;
    };

    class OverrideStack {
    public:
        uint64_t Push(std::string owner, int priority, Patch patch);
        bool Pop(uint64_t id);
        size_t RemoveOwner(const std::string& owner);
        void Clear() { m_Entries.clear(); }
        bool Empty() const { return m_Entries.empty(); }
        const std::vector<OverrideEntry>& Entries() const { return m_Entries; }

        ResolvedProfile Fold(const ResolvedProfile& base) const;

    private:
        std::vector<OverrideEntry> m_Entries;
        uint64_t m_Next = 1;
    };

    Patch IgnorePatch(const Core::TagQuery& target);
}
