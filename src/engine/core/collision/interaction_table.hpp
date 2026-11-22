#pragma once

#include <string>
#include <utility>
#include <vector>

#include "engine/core/collision/collision_classes.hpp"

namespace Shard::Engine::Collision {

    struct Action {
        std::string type;
        std::vector<std::pair<std::string, std::string>> params;

        const std::string* Param(const std::string& key) const;
        float ParamFloat(const std::string& key, float fallback) const;
    };

    struct InteractionRow {
        std::string name;
        Core::TagQuery a;
        Core::TagQuery b;
        EventKind event = EventKind::Hit;
        float minSpeed = 0.0f;
        std::vector<Action> actions;
    };

    struct InteractionMatch {
        const InteractionRow* row = nullptr;
        bool swapped = false;
        int specificity = 0;
    };

    class InteractionTable {
    public:
        std::vector<InteractionRow>& Rows() { return m_Rows; }
        const std::vector<InteractionRow>& Rows() const { return m_Rows; }
        bool Empty() const { return m_Rows.empty(); }
        uint64_t Revision() const { return m_Revision; }
        void Touch() { ++m_Revision; }

        InteractionMatch Find(const Core::TagContainer& a, const Core::TagContainer& b, EventKind event, float speed) const;
        bool MightApply(const ClassInfo& info, EventKind event) const;

    private:
        std::vector<InteractionRow> m_Rows;
        uint64_t m_Revision = 1;
    };

    int QuerySpecificity(const Core::TagQuery& query, const Core::TagContainer& tags);
}
