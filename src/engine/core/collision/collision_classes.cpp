#include "collision_classes.hpp"

#include <algorithm>

#include "engine/core/tags/tag_registry.hpp"

namespace Shard::Engine::Collision {

    namespace {
        struct Side {
            Response response = Response::Ignore;
            bool uncertain = false;
        };

        void Say(Decision* d, std::string text)
        {
            if (d) d->reasons.push_back(std::move(text));
        }

        Side EvaluateSide(const ClassInfo& self, const Core::TagContainer& other, bool exact, const char* name, Decision* trace)
        {
            Side side;
            Response upper = Response::Ignore;
            auto finish = [&](Response final) {
                side.response = side.uncertain ? Strongest(upper, final) : final;
                return side;
            };

            for (size_t i = 0; i < self.rules.size(); ++i) {
                const Rule& rule = self.rules[i];
                auto label = [&] { return std::string(name) + ": rule " + std::to_string(i + 1) + " (" + rule.target.ToString() + ")"; };
                if (self.dynamicRule[i] != 0 && !exact) {
                    upper = Strongest(upper, rule.response);
                    side.uncertain = true;
                    if (trace) Say(trace, label() + " is checked at each contact");
                    continue;
                }
                if (rule.target.Matches(other)) {
                    if (trace) Say(trace, label() + " matches : " + ToString(rule.response));
                    return finish(rule.response);
                }
            }
            if (trace) Say(trace, std::string(name) + ": no rule matches, default " + ToString(self.defaultResponse));
            return finish(self.defaultResponse);
        }
    }

    bool RuleIsDynamic(const Rule& rule)
    {
        const ProfileRegistry& registry = ProfileRegistry::Get();
        for (Core::Tag tag : rule.target.GetTags())
            if (!registry.IsCollisionTag(tag) && Core::TagRegistry::Get().IsKnown(tag))
                return true;
        return false;
    }

    std::string ClassKeyText(const ResolvedProfile& profile, bool moving)
    {
        std::string key = moving ? "M|" : "S|";
        key += ToString(profile.mode);
        key += '|';
        key += ToString(profile.defaultResponse);
        key += '|';
        key += profile.identity.ToString();
        for (const Rule& rule : profile.rules) {
            key += '|';
            key += rule.target.ToString();
            key += '=';
            key += ToString(rule.response);
        }
        return key;
    }

    void ClassTable::Grow(uint32_t slots)
    {
        if (slots <= m_Stride)
            return;
        uint32_t stride = std::max<uint32_t>(8, m_Stride);
        while (stride < slots)
            stride *= 2;
        std::vector<uint8_t> pairs(size_t(stride) * stride, 0);
        for (uint32_t a = 0; a < m_Stride; ++a)
            for (uint32_t b = 0; b < m_Stride; ++b)
                pairs[size_t(a) * stride + b] = m_Pairs[size_t(a) * m_Stride + b];
        m_Pairs = std::move(pairs);
        m_Stride = stride;
    }

    ClassId ClassTable::Acquire(const ResolvedProfile& profile, bool moving, std::string* error)
    {
        const std::string key = ClassKeyText(profile, moving);
        if (auto it = m_ByKey.find(key); it != m_ByKey.end()) {
            ++m_Classes[it->second].refs;
            return it->second;
        }

        ClassId id;
        if (!m_Free.empty()) {
            id = m_Free.back();
            m_Free.pop_back();
        } else {
            if (m_Classes.size() >= m_MaxClasses) {
                if (error)
                    *error = "more than " + std::to_string(m_MaxClasses) + " collision classes : profile '" + profile.name + "' needs a new one";
                return kInvalidClass;
            }
            id = ClassId(m_Classes.size());
            m_Classes.emplace_back();
            Grow(uint32_t(m_Classes.size()));
        }

        ClassInfo& info = m_Classes[id];
        info = ClassInfo();
        info.alive = true;
        info.refs = 1;
        info.identity = profile.identity;
        info.rules = profile.rules;
        info.defaultResponse = profile.defaultResponse;
        info.mode = profile.mode;
        info.moving = moving;
        info.key = key;
        info.label = profile.name;
        for (const Rule& rule : info.rules) {
            const bool dynamic = RuleIsDynamic(rule);
            info.dynamicRule.push_back(dynamic ? 1 : 0);
            info.hasDynamicRules = info.hasDynamicRules || dynamic;
        }
        m_ByKey.emplace(key, id);
        Recompute(id);
        ++m_Revision;
        return id;
    }

    void ClassTable::AddRef(ClassId id)
    {
        if (IsAlive(id))
            ++m_Classes[id].refs;
    }

    void ClassTable::Release(ClassId id)
    {
        if (!IsAlive(id))
            return;
        ClassInfo& info = m_Classes[id];
        if (info.refs > 0)
            --info.refs;
        if (info.refs == 0) {
            m_ByKey.erase(info.key);
            info.alive = false;
            for (uint32_t other = 0; other < m_Classes.size(); ++other) {
                m_Pairs[size_t(id) * m_Stride + other] = 0;
                m_Pairs[size_t(other) * m_Stride + id] = 0;
            }
            m_Free.push_back(id);
            ++m_Revision;
        }
    }

    void ClassTable::Recompute(ClassId id)
    {
        for (uint32_t other = 0; other < m_Classes.size(); ++other) {
            if (!m_Classes[other].alive)
                continue;
            const Decision d = Decide(id, other);
            uint8_t value = uint8_t(d.response) & kPairResponseMask;
            if (d.contactLevel)
                value |= kPairNeedsContact;
            m_Pairs[size_t(id) * m_Stride + other] = value;
            m_Pairs[size_t(other) * m_Stride + id] = value;
        }
    }

    Response ClassTable::SideResponse(ClassId self, ClassId other, bool* uncertain) const
    {
        const Side side = EvaluateSide(m_Classes[self], m_Classes[other].identity, false, "", nullptr);
        if (uncertain) *uncertain = side.uncertain;
        return side.response;
    }

    Decision ClassTable::Decide(ClassId a, ClassId b, const Core::TagContainer* liveA, const Core::TagContainer* liveB, bool trace) const
    {
        Decision d;
        Decision* t = trace ? &d : nullptr;
        const ClassInfo& ca = m_Classes[a];
        const ClassInfo& cb = m_Classes[b];

        if (ca.mode == Mode::Disabled || cb.mode == Mode::Disabled) {
            if (t) Say(t, std::string(ca.mode == Mode::Disabled ? "the first" : "the second") + " collider is Disabled");
            return d;
        }
        if (ca.mode == Mode::QueryOnly || cb.mode == Mode::QueryOnly) {
            if (t) Say(t, std::string(ca.mode == Mode::QueryOnly ? "the first" : "the second") + " collider is QueryOnly : only queries see it");
            return d;
        }
        const bool sensorA = ca.mode == Mode::Sensor;
        const bool sensorB = cb.mode == Mode::Sensor;
        if (sensorA && sensorB) {
            if (t) Say(t, "two sensors never detect each other");
            return d;
        }
        if (!ca.moving && !cb.moving) {
            if (t) Say(t, sensorA || sensorB ? "a static sensor only detects moving bodies" : "two static colliders never touch");
            return d;
        }

        const Core::TagContainer& tagsA = liveA ? *liveA : ca.identity;
        const Core::TagContainer& tagsB = liveB ? *liveB : cb.identity;
        const bool exact = liveA != nullptr || liveB != nullptr;

        const Side sideA = EvaluateSide(ca, tagsB, exact, "first", t);
        const Side sideB = EvaluateSide(cb, tagsA, exact, "second", t);
        d.contactLevel = sideA.uncertain || sideB.uncertain;

        d.response = Weakest(sideA.response, sideB.response);
        if (sensorA || sensorB) {
            d.response = Weakest(d.response, Response::Overlap);
            if (t) Say(t, "a sensor never blocks");
        }
        if (t) Say(t, std::string("result : the weaker of ") + ToString(sideA.response) + " and " + ToString(sideB.response) + " is " + ToString(d.response));
        return d;
    }

    void ClassTable::CollectAlive(std::vector<ClassId>& out) const
    {
        for (ClassId id = 0; id < m_Classes.size(); ++id)
            if (m_Classes[id].alive)
                out.push_back(id);
    }

    void ClassTable::RebuildAll()
    {
        for (ClassInfo& info : m_Classes) {
            if (!info.alive)
                continue;
            info.hasDynamicRules = false;
            for (size_t i = 0; i < info.rules.size(); ++i) {
                const bool dynamic = RuleIsDynamic(info.rules[i]);
                info.dynamicRule[i] = dynamic ? 1 : 0;
                info.hasDynamicRules = info.hasDynamicRules || dynamic;
            }
        }
        for (ClassId id = 0; id < m_Classes.size(); ++id)
            if (m_Classes[id].alive)
                Recompute(id);
        ++m_Revision;
    }

    bool ClassTable::AnyDynamic() const
    {
        for (const ClassInfo& info : m_Classes)
            if (info.alive && info.hasDynamicRules)
                return true;
        return false;
    }

    void ClassTable::Clear()
    {
        m_Classes.clear();
        m_Free.clear();
        m_ByKey.clear();
        m_Pairs.clear();
        m_Stride = 0;
        ++m_Revision;
    }
}
