#include "interaction_table.hpp"

#include <algorithm>
#include <cstdlib>

#include "engine/core/tags/tag_registry.hpp"

namespace Shard::Engine::Collision {

    const std::string* Action::Param(const std::string& key) const
    {
        for (const auto& p : params)
            if (p.first == key)
                return &p.second;
        return nullptr;
    }

    float Action::ParamFloat(const std::string& key, float fallback) const
    {
        const std::string* value = Param(key);
        if (!value)
            return fallback;
        char* end = nullptr;
        const float parsed = std::strtof(value->c_str(), &end);
        return end != value->c_str() ? parsed : fallback;
    }

    int QuerySpecificity(const Core::TagQuery& query, const Core::TagContainer& tags)
    {
        int best = 0;
        const Core::TagRegistry& registry = Core::TagRegistry::Get();
        for (Core::Tag tag : query.GetTags()) {
            if (!tags.HasTag(tag))
                continue;
            const Core::TagInfo* info = registry.GetInfo(tag);
            best = std::max(best, info ? int(info->depth) : 1);
        }
        return best;
    }

    InteractionMatch InteractionTable::Find(const Core::TagContainer& a, const Core::TagContainer& b, EventKind event, float speed) const
    {
        InteractionMatch best;
        for (const InteractionRow& row : m_Rows) {
            if (row.event != event || speed < row.minSpeed)
                continue;
            for (int pass = 0; pass < 2; ++pass) {
                const Core::TagContainer& first = pass == 0 ? a : b;
                const Core::TagContainer& second = pass == 0 ? b : a;
                if (!row.a.Matches(first) || !row.b.Matches(second))
                    continue;
                const int score = QuerySpecificity(row.a, first) + QuerySpecificity(row.b, second);
                if (!best.row || score > best.specificity) {
                    best.row = &row;
                    best.swapped = pass == 1;
                    best.specificity = score;
                }
                break;
            }
        }
        return best;
    }

    bool InteractionTable::MightApply(const ClassInfo& info, EventKind event) const
    {
        const ProfileRegistry& registry = ProfileRegistry::Get();
        auto possible = [&](const Core::TagQuery& query) {
            for (Core::Tag tag : query.GetTags())
                if (!registry.IsCollisionTag(tag))
                    return true;
            return query.Matches(info.identity);
        };
        for (const InteractionRow& row : m_Rows)
            if (row.event == event && (possible(row.a) || possible(row.b)))
                return true;
        return false;
    }
}
