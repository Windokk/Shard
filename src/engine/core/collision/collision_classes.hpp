#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/collision/collision_profile.hpp"

namespace Shard::Engine::Collision {

    using ClassId = uint32_t;
    constexpr ClassId kInvalidClass = 0xFFFFFFFFu;

    struct ClassInfo {
        bool alive = false;
        uint32_t refs = 0;
        Core::TagContainer identity;
        std::vector<Rule> rules;
        std::vector<uint8_t> dynamicRule;
        Response defaultResponse = Response::Ignore;
        Mode mode = Mode::Solid;
        bool moving = false;
        bool hasDynamicRules = false;
        std::string key;
        std::string label;
    };

    struct Decision {
        Response response = Response::Ignore;
        bool contactLevel = false;
        std::vector<std::string> reasons;
    };

    constexpr uint8_t kPairResponseMask = 0x3;
    constexpr uint8_t kPairNeedsContact = 0x4;

    class ClassTable {
    public:
        static constexpr uint32_t kDefaultMaxClasses = 4096;

        explicit ClassTable(uint32_t maxClasses = kDefaultMaxClasses) : m_MaxClasses(maxClasses) {}

        ClassId Acquire(const ResolvedProfile& profile, bool moving, std::string* error = nullptr);
        void AddRef(ClassId id);
        void Release(ClassId id);

        bool IsAlive(ClassId id) const { return id < m_Classes.size() && m_Classes[id].alive; }
        const ClassInfo& Get(ClassId id) const { return m_Classes[id]; }
        uint32_t Slots() const { return uint32_t(m_Classes.size()); }
        uint32_t AliveCount() const { return uint32_t(m_Classes.size() - m_Free.size()); }
        uint32_t MaxClasses() const { return m_MaxClasses; }
        uint64_t Revision() const { return m_Revision; }

        uint8_t PairByte(ClassId a, ClassId b) const { return m_Pairs[size_t(a) * m_Stride + b]; }
        Response PairResponse(ClassId a, ClassId b) const { return Response(PairByte(a, b) & kPairResponseMask); }
        bool PairNeedsContact(ClassId a, ClassId b) const { return (PairByte(a, b) & kPairNeedsContact) != 0; }

        Response SideResponse(ClassId self, ClassId other, bool* uncertain = nullptr) const;

        Decision Decide(ClassId a, ClassId b, const Core::TagContainer* liveA = nullptr, const Core::TagContainer* liveB = nullptr, bool trace = false) const;

        void CollectAlive(std::vector<ClassId>& out) const;
        void RebuildAll();
        bool AnyDynamic() const;
        void Clear();

    private:
        void Recompute(ClassId id);
        void Grow(uint32_t slots);

        std::vector<ClassInfo> m_Classes;
        std::vector<ClassId> m_Free;
        std::unordered_map<std::string, ClassId> m_ByKey;
        std::vector<uint8_t> m_Pairs;
        uint32_t m_Stride = 0;
        uint32_t m_MaxClasses;
        uint64_t m_Revision = 1;
    };

    bool RuleIsDynamic(const Rule& rule);
    std::string ClassKeyText(const ResolvedProfile& profile, bool moving);
}
