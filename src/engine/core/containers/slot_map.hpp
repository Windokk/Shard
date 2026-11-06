#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace Shard::Engine::Core {

    template <typename Tag>
    struct Handle {
        static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

        uint32_t index = kInvalidIndex;
        uint32_t generation = 0;

        constexpr bool IsValid() const { return index != kInvalidIndex; }
        constexpr explicit operator bool() const { return IsValid(); }
        constexpr uint64_t Pack() const { return (uint64_t(generation) << 32) | index; }
        static constexpr Handle Unpack(uint64_t packed) {
            return Handle{static_cast<uint32_t>(packed & 0xFFFFFFFFu), static_cast<uint32_t>(packed >> 32)};
        }

        friend constexpr bool operator==(Handle a, Handle b) { return a.index == b.index && a.generation == b.generation; }
        friend constexpr bool operator!=(Handle a, Handle b) { return !(a == b); }
        friend constexpr bool operator<(Handle a, Handle b) { return a.Pack() < b.Pack(); }
    };

    // Elements never move (chunked storage), so a pointer from Get stays valid until removal. Not thread-safe.
    template <typename T, typename Tag = T>
    class SlotMap {
    public:
        using HandleType = Handle<Tag>;

        SlotMap() = default;
        ~SlotMap() { Clear(); }

        SlotMap(const SlotMap&) = delete;
        SlotMap& operator=(const SlotMap&) = delete;
        SlotMap(SlotMap&& other) noexcept { *this = std::move(other); }
        SlotMap& operator=(SlotMap&& other) noexcept {
            if (this != &other) {
                Clear();
                m_Chunks = std::move(other.m_Chunks);
                m_SlotCount = other.m_SlotCount;
                m_FreeHead = other.m_FreeHead;
                m_Size = other.m_Size;
                other.m_SlotCount = 0;
                other.m_FreeHead = kNone;
                other.m_Size = 0;
            }
            return *this;
        }

        template <typename... Args>
        HandleType Emplace(Args&&... args) {
            uint32_t index;
            if (m_FreeHead != kNone) {
                index = m_FreeHead;
                m_FreeHead = SlotAt(index).nextFree;
            } else {
                index = m_SlotCount++;
                if (index / kChunkSize >= m_Chunks.size()) m_Chunks.push_back(std::make_unique<Slot[]>(kChunkSize));
            }
            Slot& slot = SlotAt(index);
            ::new (static_cast<void*>(slot.storage)) T(std::forward<Args>(args)...);
            slot.alive = true;
            ++m_Size;
            return HandleType{index, slot.generation};
        }

        HandleType Insert(T value) { return Emplace(std::move(value)); }

        bool Remove(HandleType handle) {
            Slot* slot = Find(handle);
            if (!slot) return false;
            reinterpret_cast<T*>(slot->storage)->~T();
            slot->alive = false;
            if (++slot->generation == 0) slot->generation = 1;
            slot->nextFree = m_FreeHead;
            m_FreeHead = handle.index;
            --m_Size;
            return true;
        }

        T* Get(HandleType handle) {
            Slot* slot = Find(handle);
            return slot ? reinterpret_cast<T*>(slot->storage) : nullptr;
        }
        const T* Get(HandleType handle) const { return const_cast<SlotMap*>(this)->Get(handle); }

        bool Contains(HandleType handle) const { return Get(handle) != nullptr; }

        size_t Size() const { return m_Size; }
        bool Empty() const { return m_Size == 0; }
        size_t SlotCount() const { return m_SlotCount; }

        void Clear() {
            for (uint32_t i = 0; i < m_SlotCount; ++i) {
                Slot& slot = SlotAt(i);
                if (slot.alive) {
                    reinterpret_cast<T*>(slot.storage)->~T();
                    slot.alive = false;
                }
                if (++slot.generation == 0) slot.generation = 1;
            }
            m_FreeHead = kNone;
            for (uint32_t i = m_SlotCount; i-- > 0;) {
                SlotAt(i).nextFree = m_FreeHead;
                m_FreeHead = i;
            }
            m_Size = 0;
        }

        template <typename Fn>
        void ForEach(Fn&& fn) {
            for (uint32_t i = 0; i < m_SlotCount; ++i) {
                Slot& slot = SlotAt(i);
                if (slot.alive) fn(HandleType{i, slot.generation}, *reinterpret_cast<T*>(slot.storage));
            }
        }
        template <typename Fn>
        void ForEach(Fn&& fn) const { const_cast<SlotMap*>(this)->ForEach([&](HandleType h, T& v) { fn(h, static_cast<const T&>(v)); }); }

        HandleType HandleAt(uint32_t index) const {
            if (index >= m_SlotCount) return HandleType{};
            const Slot& slot = const_cast<SlotMap*>(this)->SlotAt(index);
            return slot.alive ? HandleType{index, slot.generation} : HandleType{};
        }

        class Iterator {
        public:
            Iterator(SlotMap* map, uint32_t index) : m_Map(map), m_Index(index) { SkipDead(); }
            std::pair<HandleType, T&> operator*() const {
                Slot& slot = m_Map->SlotAt(m_Index);
                return {HandleType{m_Index, slot.generation}, *reinterpret_cast<T*>(slot.storage)};
            }
            Iterator& operator++() { ++m_Index; SkipDead(); return *this; }
            bool operator==(const Iterator& o) const { return m_Index == o.m_Index; }
            bool operator!=(const Iterator& o) const { return m_Index != o.m_Index; }
        private:
            void SkipDead() { while (m_Index < m_Map->m_SlotCount && !m_Map->SlotAt(m_Index).alive) ++m_Index; }
            SlotMap* m_Map;
            uint32_t m_Index;
        };

        Iterator begin() { return Iterator(this, 0); }
        Iterator end() { return Iterator(this, m_SlotCount); }

    private:
        static constexpr uint32_t kNone = 0xFFFFFFFFu;
        static constexpr uint32_t kChunkSize = 256;

        struct Slot {
            alignas(T) unsigned char storage[sizeof(T)];
            uint32_t generation = 1;
            uint32_t nextFree = kNone;
            bool alive = false;
        };

        Slot& SlotAt(uint32_t index) { return m_Chunks[index / kChunkSize][index % kChunkSize]; }

        Slot* Find(HandleType handle) {
            if (handle.index >= m_SlotCount) return nullptr;
            Slot& slot = SlotAt(handle.index);
            return (slot.alive && slot.generation == handle.generation) ? &slot : nullptr;
        }

        std::vector<std::unique_ptr<Slot[]>> m_Chunks;
        uint32_t m_SlotCount = 0;
        uint32_t m_FreeHead = kNone;
        size_t m_Size = 0;
    };
}

namespace std {
    template <typename Tag>
    struct hash<Shard::Engine::Core::Handle<Tag>> {
        size_t operator()(Shard::Engine::Core::Handle<Tag> h) const noexcept { return std::hash<uint64_t>()(h.Pack()); }
    };
}
