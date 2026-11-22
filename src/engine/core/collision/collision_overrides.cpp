#include "collision_overrides.hpp"

#include <algorithm>

namespace Shard::Engine::Collision {

    uint64_t OverrideStack::Push(std::string owner, int priority, Patch patch)
    {
        OverrideEntry entry;
        entry.id = m_Next++;
        entry.owner = std::move(owner);
        entry.priority = priority;
        entry.patch = std::move(patch);
        m_Entries.push_back(std::move(entry));
        return m_Entries.back().id;
    }

    bool OverrideStack::Pop(uint64_t id)
    {
        auto it = std::find_if(m_Entries.begin(), m_Entries.end(), [&](const OverrideEntry& e) { return e.id == id; });
        if (it == m_Entries.end())
            return false;
        m_Entries.erase(it);
        return true;
    }

    size_t OverrideStack::RemoveOwner(const std::string& owner)
    {
        const size_t before = m_Entries.size();
        m_Entries.erase(std::remove_if(m_Entries.begin(), m_Entries.end(), [&](const OverrideEntry& e) { return e.owner == owner; }), m_Entries.end());
        return before - m_Entries.size();
    }

    ResolvedProfile OverrideStack::Fold(const ResolvedProfile& base) const
    {
        if (m_Entries.empty())
            return base;

        std::vector<const OverrideEntry*> ordered;
        ordered.reserve(m_Entries.size());
        for (const OverrideEntry& e : m_Entries)
            ordered.push_back(&e);
        std::stable_sort(ordered.begin(), ordered.end(), [](const OverrideEntry* a, const OverrideEntry* b) {
            if (a->priority != b->priority)
                return a->priority > b->priority;
            return a->id > b->id;
        });

        ResolvedProfile out = base;
        std::vector<Rule> rules;
        std::optional<Mode> mode;
        std::optional<Response> fallback;
        for (const OverrideEntry* e : ordered) {
            rules.insert(rules.end(), e->patch.rules.begin(), e->patch.rules.end());
            if (!mode && e->patch.mode) mode = e->patch.mode;
            if (!fallback && e->patch.defaultResponse) fallback = e->patch.defaultResponse;
            for (Core::Tag tag : FilterCollisionTags(e->patch.addIdentity))
                out.identity.Add(tag);
        }
        rules.insert(rules.end(), base.rules.begin(), base.rules.end());
        out.rules = std::move(rules);
        if (mode) out.mode = *mode;
        if (fallback) out.defaultResponse = *fallback;
        return out;
    }

    Patch IgnorePatch(const Core::TagQuery& target)
    {
        Patch patch;
        patch.rules.push_back({ target, Response::Ignore });
        return patch;
    }
}
