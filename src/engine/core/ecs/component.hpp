#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Shard::Engine::Core::Ecs {

    /// A small dense integer that stands for a C++ type. Components, but also anything else a system wants to declare
    /// access to (a "resource" such as the physics world) : the scheduler only compares ids.
    using ComponentId = uint32_t;
    constexpr ComponentId kInvalidComponent = ~0u;

    struct ComponentInfo {
        ComponentId id = kInvalidComponent;
        size_t size = 0;
        size_t align = 1;
        std::string name;
    };

    /// @brief Hands out the ComponentId of each type, once, process-wide. Thread-safe.
    ///
    /// The id is looked up by the type's name rather than by a per-module static : the game module (a DLL) and the
    /// engine then agree on the id of a type they both include.
    class ComponentRegistry {
    public:
        static ComponentRegistry& Global();

        ComponentId Register(const char* uniqueName, size_t size, size_t align);

        /// The info of an id. Valid for as long as the registry lives (entries never move).
        const ComponentInfo& Info(ComponentId id) const;

        size_t Count() const;

    private:
        mutable std::mutex m_Mutex;
        std::vector<ComponentInfo*> m_Infos;                          // stable addresses
        std::unordered_map<std::string, ComponentId> m_ByName;
    };

    /// @brief The id of T, registered on first use. For a component T must be trivially copyable (checked by the Registry).
    template <typename T>
    ComponentId ComponentIdOf() {
        static const ComponentId id = ComponentRegistry::Global().Register(typeid(T).name(), sizeof(T), alignof(T));
        return id;
    }

    /// Sorted, duplicate-free list of ids : the "signature" that identifies an archetype and a query.
    using ComponentSet = std::vector<ComponentId>;

    inline void Normalize(ComponentSet& set) {
        // insertion sort + unique : sets hold a handful of ids
        for (size_t i = 1; i < set.size(); ++i)
            for (size_t j = i; j > 0 && set[j - 1] > set[j]; --j) std::swap(set[j - 1], set[j]);
        size_t out = 0;
        for (size_t i = 0; i < set.size(); ++i)
            if (i == 0 || set[i] != set[i - 1]) set[out++] = set[i];
        set.resize(out);
    }

    inline bool Contains(const ComponentSet& sortedSet, ComponentId id) {
        size_t lo = 0, hi = sortedSet.size();
        while (lo < hi) {
            const size_t mid = (lo + hi) / 2;
            if (sortedSet[mid] < id) lo = mid + 1; else hi = mid;
        }
        return lo < sortedSet.size() && sortedSet[lo] == id;
    }

    /// True if every id of `subset` is in `set` (both sorted).
    inline bool IsSubset(const ComponentSet& subset, const ComponentSet& set) {
        size_t j = 0;
        for (ComponentId id : subset) {
            while (j < set.size() && set[j] < id) ++j;
            if (j == set.size() || set[j] != id) return false;
            ++j;
        }
        return true;
    }

    inline bool Intersects(const ComponentSet& a, const ComponentSet& b) {
        size_t i = 0, j = 0;
        while (i < a.size() && j < b.size()) {
            if (a[i] == b[j]) return true;
            if (a[i] < b[j]) ++i; else ++j;
        }
        return false;
    }
}
