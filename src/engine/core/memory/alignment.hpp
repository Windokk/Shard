#pragma once

#include <cstddef>
#include <cstdint>
#include <new>

namespace Shard::Engine::Core {

    constexpr size_t kDefaultAlignment = alignof(std::max_align_t);

    constexpr bool IsPowerOfTwo(size_t value) { return value != 0 && (value & (value - 1)) == 0; }

    template <typename T>
    constexpr T AlignUp(T value, size_t alignment) {
        return static_cast<T>((value + static_cast<T>(alignment) - 1) & ~(static_cast<T>(alignment) - 1));
    }

    inline bool IsAligned(const void* ptr, size_t alignment) {
        return (reinterpret_cast<uintptr_t>(ptr) & (alignment - 1)) == 0;
    }

    inline void* AlignedAlloc(size_t size, size_t alignment = kDefaultAlignment) {
        return ::operator new(size == 0 ? 1 : size, std::align_val_t(alignment), std::nothrow);
    }

    inline void AlignedFree(void* ptr, size_t alignment = kDefaultAlignment) {
        if (ptr) ::operator delete(ptr, std::align_val_t(alignment));
    }
}
