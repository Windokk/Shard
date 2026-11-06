#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace Shard::Engine::Core::Ecs {

    // Low 32 bits index, high 32 generation. Generation 0 is never used, so a zero Entity is null.
    struct Entity {
        uint64_t value = 0;

        constexpr Entity() = default;
        constexpr explicit Entity(uint64_t raw) : value(raw) {}

        static constexpr Entity Make(uint32_t index, uint32_t generation) {
            return Entity((static_cast<uint64_t>(generation) << 32) | index);
        }

        constexpr uint32_t Index() const { return static_cast<uint32_t>(value); }
        constexpr uint32_t Generation() const { return static_cast<uint32_t>(value >> 32); }
        constexpr bool IsNull() const { return value == 0; }
        constexpr explicit operator bool() const { return value != 0; }

        friend constexpr bool operator==(Entity a, Entity b) { return a.value == b.value; }
        friend constexpr bool operator!=(Entity a, Entity b) { return a.value != b.value; }
        friend constexpr bool operator<(Entity a, Entity b) { return a.value < b.value; }
    };

    constexpr Entity kNullEntity{};
}

template <>
struct std::hash<Shard::Engine::Core::Ecs::Entity> {
    size_t operator()(Shard::Engine::Core::Ecs::Entity e) const noexcept { return std::hash<uint64_t>()(e.value); }
};
