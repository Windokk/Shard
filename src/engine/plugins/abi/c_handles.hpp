#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "engine/plugins/context.hpp"

// Handle table: generation + slot, so a stale handle finds nothing. Entries are per plugin. Thread-safe; Get returns a shared_ptr so a removed entry outlives concurrent use.

namespace Shard::Engine::Plugins::CAbi {

    template <typename T>
    class OwnedTable {
    public:
        // 0 is never a handle
        uint64_t Add(PluginOwner owner, std::shared_ptr<T> value)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            uint32_t index;
            if (!m_Free.empty()) { index = m_Free.back(); m_Free.pop_back(); }
            else { index = uint32_t(m_Slots.size()); m_Slots.emplace_back(); }
            Slot& slot = m_Slots[index];
            slot.owner = owner;
            slot.value = std::move(value);
            return (uint64_t(slot.generation) << 32) | uint64_t(index + 1);
        }

        std::shared_ptr<T> Get(PluginOwner owner, uint64_t handle) const
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            const Slot* slot = Find(handle);
            return slot && slot->owner == owner ? slot->value : nullptr;
        }

        std::shared_ptr<T> GetAny(uint64_t handle) const
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            const Slot* slot = Find(handle);
            return slot ? slot->value : nullptr;
        }

        bool Remove(PluginOwner owner, uint64_t handle, std::shared_ptr<T>* removed = nullptr)
        {
            std::shared_ptr<T> value;
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                Slot* slot = Find(handle);
                if (!slot || slot->owner != owner) return false;
                value = std::move(slot->value);
                Free(*slot, uint32_t((handle & 0xFFFFFFFFu) - 1));
            }
            if (removed) *removed = std::move(value);
            return true;
        }

        std::vector<std::shared_ptr<T>> RemoveOwner(PluginOwner owner)
        {
            std::vector<std::shared_ptr<T>> out;
            std::lock_guard<std::mutex> lock(m_Mutex);
            for (uint32_t i = 0; i < m_Slots.size(); ++i) {
                Slot& slot = m_Slots[i];
                if (slot.owner != owner || !slot.value) continue;
                out.push_back(std::move(slot.value));
                Free(slot, i);
            }
            return out;
        }

        // Lock is held while fn runs, fn must not call back in
        template <typename Fn>
        void ForEach(PluginOwner owner, Fn&& fn) const
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            for (uint32_t i = 0; i < m_Slots.size(); ++i) {
                const Slot& slot = m_Slots[i];
                if (slot.owner == owner && slot.value) fn((uint64_t(slot.generation) << 32) | uint64_t(i + 1), *slot.value);
            }
        }

        template <typename Fn>
        void ForEachAll(Fn&& fn) const
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            for (uint32_t i = 0; i < m_Slots.size(); ++i) {
                const Slot& slot = m_Slots[i];
                if (slot.value) fn((uint64_t(slot.generation) << 32) | uint64_t(i + 1), *slot.value, slot.owner);
            }
        }

        size_t CountOf(PluginOwner owner) const
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            size_t n = 0;
            for (const Slot& slot : m_Slots) if (slot.owner == owner && slot.value) ++n;
            return n;
        }

    private:
        struct Slot {
            PluginOwner owner = 0;
            uint32_t generation = 1;
            std::shared_ptr<T> value;
        };

        const Slot* Find(uint64_t handle) const
        {
            const uint32_t index = uint32_t(handle & 0xFFFFFFFFu);
            if (index == 0 || index > m_Slots.size()) return nullptr;
            const Slot& slot = m_Slots[index - 1];
            return slot.value && slot.generation == uint32_t(handle >> 32) ? &slot : nullptr;
        }
        Slot* Find(uint64_t handle) { return const_cast<Slot*>(static_cast<const OwnedTable*>(this)->Find(handle)); }

        void Free(Slot& slot, uint32_t index)
        {
            slot.owner = 0;
            if (++slot.generation == 0) slot.generation = 1;
            m_Free.push_back(index);
        }

        mutable std::mutex m_Mutex;
        std::vector<Slot> m_Slots;
        std::vector<uint32_t> m_Free;
    };
}
