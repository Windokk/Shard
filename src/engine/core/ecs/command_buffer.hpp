#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include "engine/core/ecs/component.hpp"
#include "engine/core/ecs/entity.hpp"

namespace Shard::Engine::Core::Ecs {

    // Structural changes are recorded now and applied later with Registry::Playback, since adding a component moves memory under iterators.
    // Create() returns a placeholder valid for later commands of the same buffer.
    class CommandBuffer {
    public:
        enum class Op : uint8_t { Create, Destroy, Add, Remove, SetParent };

        struct Command {
            Op op;
            Entity target;
            Entity other;
            ComponentId component = kInvalidComponent;
            uint32_t size = 0;
            size_t dataOffset = 0;
        };

        // A placeholder is recognisable by its generation, which no real entity reaches
        static constexpr uint32_t kPlaceholderGeneration = ~0u;
        static bool IsPlaceholder(Entity e) { return e.Generation() == kPlaceholderGeneration; }

        Entity Create() {
            const Entity placeholder = Entity::Make(m_Placeholders++, kPlaceholderGeneration);
            m_Commands.push_back(Command{Op::Create, placeholder, kNullEntity});
            return placeholder;
        }

        void Destroy(Entity e) { m_Commands.push_back(Command{Op::Destroy, e, kNullEntity}); }

        template <typename T>
        void Add(Entity e, const T& value) {
            static_assert(std::is_trivially_copyable<T>::value, "components must be trivially copyable (POD-like)");
            AddRaw(e, ComponentIdOf<T>(), &value, sizeof(T));
        }

        void AddRaw(Entity e, ComponentId id, const void* data, size_t size) {
            Command c{Op::Add, e, kNullEntity, id, static_cast<uint32_t>(size), m_Data.size()};
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            m_Data.insert(m_Data.end(), bytes, bytes + size);
            m_Commands.push_back(c);
        }

        template <typename T>
        void Remove(Entity e) { RemoveRaw(e, ComponentIdOf<T>()); }

        void RemoveRaw(Entity e, ComponentId id) { m_Commands.push_back(Command{Op::Remove, e, kNullEntity, id}); }

        void SetParent(Entity child, Entity parent) { m_Commands.push_back(Command{Op::SetParent, child, parent}); }

        size_t Size() const { return m_Commands.size(); }
        bool Empty() const { return m_Commands.empty(); }
        void Clear() { m_Commands.clear(); m_Data.clear(); m_Placeholders = 0; }

    private:
        friend class Registry;

        std::vector<Command> m_Commands;
        std::vector<uint8_t> m_Data;
        uint32_t m_Placeholders = 0;
    };
}
