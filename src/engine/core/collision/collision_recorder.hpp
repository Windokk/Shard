#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Shard::Engine::Collision {

    enum class RecordKind : uint8_t { Contact = 0, Event = 1, Query = 2, Override = 3 };

    struct Record {
        uint64_t step = 0;
        RecordKind kind = RecordKind::Contact;
        uint8_t detail = 0;
        uint16_t extra = 0;
        uint64_t a = 0;
        uint64_t b = 0;
        float value = 0.0f;
        double position[3] = { 0.0, 0.0, 0.0 };
    };

    class Recorder {
    public:
        static constexpr size_t kDefaultBudget = 8u * 1024u * 1024u;

        explicit Recorder(size_t budgetBytes = kDefaultBudget);

        void SetBudget(size_t budgetBytes);
        void SetEnabled(bool enabled) { m_Enabled = enabled; }
        bool Enabled() const { return m_Enabled; }

        void Push(const Record& record);
        size_t Size() const { return m_Count; }
        size_t Capacity() const { return m_Records.size(); }
        const Record& At(size_t index) const;
        void Clear();

        uint64_t FirstStep() const;
        uint64_t LastStep() const;
        std::vector<Record> Step(uint64_t step) const;
        uint64_t StreamHash() const;

    private:
        std::vector<Record> m_Records;
        size_t m_Head = 0;
        size_t m_Count = 0;
        bool m_Enabled = true;
    };
}
