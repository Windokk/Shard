#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/plugins/hooks.hpp"

namespace Shard::Engine::Plugins {

    // Named extension points. The owning subsystem defines the item type and reads the items, the registry knows nothing about them. Items are shared_ptr<void> tagged with a type name, checked on read.
    class ExtensionRegistry {
    public:
        struct Item {
            std::string point;
            std::string name;
            std::string typeName;
            std::shared_ptr<void> object;
            PluginOwner owner = 0;
        };

        using Listener = std::function<void(const Item&, bool added)>;

        template <typename T>
        bool Add(const std::string& point, const std::string& name, std::shared_ptr<T> object, PluginOwner owner)
        {
            return AddRaw(Item{ point, name, typeid(T).name(), std::static_pointer_cast<void>(object), owner });
        }

        template <typename T>
        std::vector<std::shared_ptr<T>> Get(const std::string& point) const
        {
            std::vector<std::shared_ptr<T>> out;
            for (const Item& item : m_Items)
                if (item.point == point && item.typeName == typeid(T).name())
                    out.push_back(std::static_pointer_cast<T>(item.object));
            return out;
        }

        std::vector<Item> List(const std::string& point = {}) const
        {
            std::vector<Item> out;
            for (const Item& item : m_Items) if (point.empty() || item.point == point) out.push_back(item);
            return out;
        }

        bool Remove(const std::string& point, const std::string& name, PluginOwner owner)
        {
            for (auto it = m_Items.begin(); it != m_Items.end(); ++it) {
                if (it->point == point && it->name == name && it->owner == owner) {
                    Item removed = std::move(*it);
                    m_Items.erase(it);
                    Notify(removed, false);
                    return true;
                }
            }
            return false;
        }

        size_t RemoveOwner(PluginOwner owner)
        {
            size_t count = 0;
            for (size_t i = 0; i < m_Items.size();) {
                if (m_Items[i].owner == owner) {
                    Item removed = std::move(m_Items[i]);
                    m_Items.erase(m_Items.begin() + long(i));
                    Notify(removed, false);
                    ++count;
                } else ++i;
            }
            return count;
        }

        uint32_t Listen(const std::string& point, Listener listener)
        {
            const uint32_t id = ++m_NextListener;
            for (const Item& item : m_Items) if (item.point == point) listener(item, true);
            m_Listeners.push_back({ id, point, std::move(listener) });
            return id;
        }

        void Unlisten(uint32_t id)
        {
            m_Listeners.erase(std::remove_if(m_Listeners.begin(), m_Listeners.end(), [&](const L& l) { return l.id == id; }), m_Listeners.end());
        }

    private:
        struct L { uint32_t id; std::string point; Listener fn; };

        bool AddRaw(Item item)
        {
            for (const Item& e : m_Items)
                if (e.point == item.point && e.name == item.name) return false;
            m_Items.push_back(item);
            Notify(item, true);
            return true;
        }

        void Notify(const Item& item, bool added)
        {
            const std::vector<L> copy = m_Listeners;
            for (const L& l : copy) if (l.point == item.point) l.fn(item, added);
        }

        std::vector<Item> m_Items;
        std::vector<L> m_Listeners;
        uint32_t m_NextListener = 0;
    };
}
