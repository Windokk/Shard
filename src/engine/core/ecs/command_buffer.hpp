#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include "engine/core/ecs/component.hpp"
#include "engine/core/ecs/entity.hpp"

namespace Shard::Engine::Core::Ecs {

    /// @brief A list of structural changes (create / destroy an entity, add / remove a component, set the parent) recorded now
    /// and applied later, in one go, with Registry::Playback.
    ///
    /// Why : adding a component moves the entity to another archetype, i.e. moves memory under the feet of whoever is iterating.
    /// While a query runs, structural changes are refused ; a system records them here instead, and they are applied when it is
    /// safe (the end of the phase). Each system owns its buffer, so recording needs no lock, and playing the buffers back in a
    /// fixed order makes the result the same whether the systems ran in parallel or not.
    ///
    /// Create() returns a PLACEHOLDER entity : a stand-in that later commands of the SAME buffer can use ("create, then add
    /// Position to it, then parent it under this one"). Playback maps each placeholder to the entity it really created.
    class CommandBuffer {
    public:
        enum class Op : uint8_t { Create, Destroy, Add, Remove, SetParent };

        struct Command {
            Op op;
            Entity target;              // the entity (or placeholder) the command is about
            Entity other;               // SetParent : the parent
            ComponentId component = kInvalidComponent;
            uint32_t size = 0;          // Add : bytes of the component value
            size_t dataOffset = 0;      // Add : where those bytes are in m_Data
        };

        /// A placeholder is recognisable by its generation, which no real entity ever reaches
        static constexpr uint32_t kPlaceholderGeneration = ~0u;
        static bool IsPlaceholder(Entity e) { return e.Generation() == kPlaceholderGeneration; }

        /// Records "create an entity". The returned id only means something to the commands of this buffer.
        Entity Create() {
            const Entity placeholder = Entity::Make(m_Placeholders++, kPlaceholderGeneration);
            m_Commands.push_back(Command{Op::Create, placeholder, kNullEntity});
            return placeholder;
        }

        void Destroy(Entity e) { m_Commands.push_back(Command{Op::Destroy, e, kNullEntity}); }

        /// Records "add (or overwrite) component T on e". The value is copied now.
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

        /// Records "make `parent` the parent of `child`" (a null parent detaches it)
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
