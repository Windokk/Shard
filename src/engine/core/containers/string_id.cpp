#include "engine/core/containers/string_id.hpp"

#include <cassert>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace Shard::Engine::Core {

    namespace {
        struct Registry {
            std::mutex mutex;
            std::unordered_map<uint64_t, std::string> names;
            std::atomic<bool> collision{false};
        };

        Registry& GetRegistry() {
            static Registry registry;
            return registry;
        }
    }

    StringId StringId::Intern(std::string_view text) {
        StringId id(text);
        Registry& registry = GetRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        auto [it, inserted] = registry.names.emplace(id.m_Hash, std::string(text));
        if (!inserted && it->second != text) {
            registry.collision.store(true);
            assert(false && "StringId hash collision between two different names");
        }
        return id;
    }

    std::string StringId::ToString() const {
        Registry& registry = GetRegistry();
        {
            std::lock_guard<std::mutex> lock(registry.mutex);
            auto it = registry.names.find(m_Hash);
            if (it != registry.names.end()) return it->second;
        }
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "#%016llx", static_cast<unsigned long long>(m_Hash));
        return buffer;
    }

    bool StringId::HadCollision() { return GetRegistry().collision.load(); }
}
