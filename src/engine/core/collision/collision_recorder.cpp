#include "collision_recorder.hpp"

#include <algorithm>

namespace Shard::Engine::Collision {

    Recorder::Recorder(size_t budgetBytes)
    {
        SetBudget(budgetBytes);
    }

    void Recorder::SetBudget(size_t budgetBytes)
    {
        const size_t capacity = std::max<size_t>(16, budgetBytes / sizeof(Record));
        std::vector<Record> kept;
        kept.reserve(std::min(m_Count, capacity));
        const size_t skip = m_Count > capacity ? m_Count - capacity : 0;
        for (size_t i = skip; i < m_Count; ++i)
            kept.push_back(At(i));
        m_Records.assign(capacity, Record());
        for (size_t i = 0; i < kept.size(); ++i)
            m_Records[i] = kept[i];
        m_Count = kept.size();
        m_Head = m_Count % capacity;
    }

    void Recorder::Push(const Record& record)
    {
        if (!m_Enabled || m_Records.empty())
            return;
        m_Records[m_Head] = record;
        m_Head = (m_Head + 1) % m_Records.size();
        if (m_Count < m_Records.size())
            ++m_Count;
    }

    const Record& Recorder::At(size_t index) const
    {
        const size_t start = (m_Head + m_Records.size() - m_Count) % m_Records.size();
        return m_Records[(start + index) % m_Records.size()];
    }

    void Recorder::Clear()
    {
        m_Head = 0;
        m_Count = 0;
    }

    uint64_t Recorder::FirstStep() const
    {
        return m_Count ? At(0).step : 0;
    }

    uint64_t Recorder::LastStep() const
    {
        return m_Count ? At(m_Count - 1).step : 0;
    }

    std::vector<Record> Recorder::Step(uint64_t step) const
    {
        std::vector<Record> out;
        for (size_t i = 0; i < m_Count; ++i) {
            const Record& r = At(i);
            if (r.step == step)
                out.push_back(r);
            else if (r.step > step)
                break;
        }
        return out;
    }

    uint64_t Recorder::StreamHash() const
    {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](uint64_t v) {
            for (int i = 0; i < 8; ++i) {
                h ^= (v >> (i * 8)) & 0xFF;
                h *= 1099511628211ull;
            }
        };
        for (size_t i = 0; i < m_Count; ++i) {
            const Record& r = At(i);
            if (r.kind != RecordKind::Event)
                continue;
            mix(r.step);
            mix(r.detail);
            mix(r.a);
            mix(r.b);
        }
        return h;
    }
}
