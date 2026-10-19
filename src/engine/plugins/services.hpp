#pragma once

#include <atomic>
#include <cassert>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/plugins/hooks.hpp"
#include "engine/plugins/version.hpp"

namespace Shard::Engine::Plugins {

    /// What a ServiceRef shares with the registry : the registry clears it when the provider goes away
    struct ServiceSlot {
        std::atomic<void*> instance{ nullptr };
    };

    /// @brief A handle on a service another plugin provides. It becomes empty when the provider is disabled or unloaded, so check
    /// it (`if (ref)`) before each use rather than keeping the raw pointer.
    template <typename T>
    class ServiceRef {
    public:
        ServiceRef() = default;
        explicit ServiceRef(std::shared_ptr<ServiceSlot> slot) : m_Slot(std::move(slot)) {}

        explicit operator bool() const { return Get() != nullptr; }
        T* Get() const { return m_Slot ? static_cast<T*>(m_Slot->instance.load(std::memory_order_acquire)) : nullptr; }
        T* operator->() const { T* p = Get(); assert(p && "ServiceRef used after its provider went away"); return p; }
        T& operator*() const { T* p = Get(); assert(p && "ServiceRef used after its provider went away"); return *p; }

    private:
        std::shared_ptr<ServiceSlot> m_Slot;
    };

    /// @brief The interfaces plugins offer to each other, by name and version. Plugins never include each other's headers : one
    /// provides `weather.query` 1.0, another asks for `weather.query` ">=1" and gets a handle.
    class ServiceRegistry {
    public:
        struct Info {
            std::string name;
            SemVer version;
            PluginOwner owner = 0;
        };

        /// @return false if a service of this name is already provided
        bool Provide(const std::string& name, const SemVer& version, void* instance, PluginOwner owner)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_Services.count(name)) return false;
            Entry e;
            e.info = Info{ name, version, owner };
            e.slot = std::make_shared<ServiceSlot>();
            e.slot->instance.store(instance, std::memory_order_release);
            m_Services.emplace(name, std::move(e));
            return true;
        }

        /// @brief An empty ref when the service does not exist or its version is outside `range`
        template <typename T>
        ServiceRef<T> Get(const std::string& name, const VersionRange& range = VersionRange::Any()) const
        {
            return ServiceRef<T>(Find(name, range));
        }

        template <typename T>
        ServiceRef<T> Get(const std::string& name, const std::string& range) const
        {
            auto parsed = VersionRange::Parse(range);
            return parsed ? ServiceRef<T>(Find(name, *parsed)) : ServiceRef<T>();
        }

        bool Has(const std::string& name) const
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return m_Services.count(name) != 0;
        }

        bool Revoke(const std::string& name, PluginOwner owner)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto it = m_Services.find(name);
            if (it == m_Services.end() || it->second.info.owner != owner) return false;
            it->second.slot->instance.store(nullptr, std::memory_order_release);
            m_Services.erase(it);
            return true;
        }

        size_t RevokeOwner(PluginOwner owner)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            size_t count = 0;
            for (auto it = m_Services.begin(); it != m_Services.end();) {
                if (it->second.info.owner == owner) {
                    it->second.slot->instance.store(nullptr, std::memory_order_release);
                    it = m_Services.erase(it);
                    ++count;
                } else ++it;
            }
            return count;
        }

        std::vector<Info> List() const
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            std::vector<Info> out;
            for (const auto& kv : m_Services) out.push_back(kv.second.info);
            std::sort(out.begin(), out.end(), [](const Info& a, const Info& b) { return a.name < b.name; });
            return out;
        }

    private:
        struct Entry {
            Info info;
            std::shared_ptr<ServiceSlot> slot;
        };

        std::shared_ptr<ServiceSlot> Find(const std::string& name, const VersionRange& range) const
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto it = m_Services.find(name);
            if (it == m_Services.end() || !range.Satisfies(it->second.info.version)) return nullptr;
            return it->second.slot;
        }

        mutable std::mutex m_Mutex;
        std::unordered_map<std::string, Entry> m_Services;
    };
}
