#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Shard::Engine::Plugins {

    /// The plugin that registered something (0 : the engine or the application itself). Owners are numbered in load order, so
    /// sorting by owner is sorting by load order.
    using PluginOwner = uint32_t;

    enum class Priority : int { Earliest = -200, Early = -100, Normal = 0, Late = 100, Latest = 200 };

    /// What a hook passes to its handlers : any of them may cancel the thing that is about to happen.
    struct HookBase {
        bool cancelled = false;
        std::string reason;
        PluginOwner cancelledBy = 0;
        void Cancel(std::string why = {}) { cancelled = true; reason = std::move(why); }
    };

    /// The points of the engine a plugin can intercept. Each type names itself (kName) : hooks are found by that name, never by
    /// C++ type identity, which is not reliable between an executable and a library.
    namespace Hooks {
        struct PreEngineInit : HookBase { static constexpr const char* kName = "PreEngineInit"; };
        struct PostEngineInit : HookBase { static constexpr const char* kName = "PostEngineInit"; };
        struct PreWorldLoad : HookBase {
            static constexpr const char* kName = "PreWorldLoad";
            std::string worldName;
            explicit PreWorldLoad(std::string name = {}) : worldName(std::move(name)) {}
        };
        struct PostWorldLoad : HookBase {
            static constexpr const char* kName = "PostWorldLoad";
            std::string worldName;
            explicit PostWorldLoad(std::string name = {}) : worldName(std::move(name)) {}
        };
        struct PrePlay : HookBase { static constexpr const char* kName = "PrePlay"; };
        struct PostPlay : HookBase { static constexpr const char* kName = "PostPlay"; };
        struct PreFrame : HookBase {
            static constexpr const char* kName = "PreFrame";
            uint64_t frame = 0;
            explicit PreFrame(uint64_t f = 0) : frame(f) {}
        };
        struct PostFrame : HookBase {
            static constexpr const char* kName = "PostFrame";
            uint64_t frame = 0;
            explicit PostFrame(uint64_t f = 0) : frame(f) {}
        };
        struct PreSave : HookBase {
            static constexpr const char* kName = "PreSave";
            std::string what;           ///< the world or the project being saved
            explicit PreSave(std::string w = {}) : what(std::move(w)) {}
        };
        struct AssetImported : HookBase {
            static constexpr const char* kName = "AssetImported";
            std::string path;
            explicit AssetImported(std::string p = {}) : path(std::move(p)) {}
        };
        struct PlayerConnecting : HookBase {
            static constexpr const char* kName = "PlayerConnecting";
            std::string clientName;
            std::string address;
            PlayerConnecting(std::string n = {}, std::string a = {}) : clientName(std::move(n)), address(std::move(a)) {}
        };
        struct PreShutdown : HookBase { static constexpr const char* kName = "PreShutdown"; };
    }

    /// @brief The handlers plugins attached to the hooks. Handlers run by priority, then by owner (= plugin load order), then in
    /// the order they were added : the same order on every run.
    class HookRegistry {
    public:
        using Token = uint64_t;

        template <typename H>
        Token On(Priority priority, std::function<void(H&)> handler, PluginOwner owner)
        {
            Entry e;
            e.token = ++m_Next;
            e.priority = static_cast<int>(priority);
            e.owner = owner;
            e.fn = [h = std::move(handler)](HookBase& base) { h(static_cast<H&>(base)); };
            auto& list = m_Hooks[H::kName];
            list.push_back(std::move(e));
            std::stable_sort(list.begin(), list.end(), [](const Entry& a, const Entry& b) {
                if (a.priority != b.priority) return a.priority < b.priority;
                if (a.owner != b.owner) return a.owner < b.owner;
                return a.token < b.token;
            });
            return list.empty() ? 0 : m_Next;
        }

        /// @brief Runs the handlers of H. A handler that cancels does not stop the ones after it (they can see `cancelled` and
        /// decide for themselves) ; `cancelledBy` is the first one that cancelled.
        /// @return false if the hook was cancelled
        template <typename H>
        bool Emit(H& hook)
        {
            auto it = m_Hooks.find(H::kName);
            if (it == m_Hooks.end()) return true;
            ++m_Depth;
            const std::vector<Entry> snapshot = it->second;      // handlers may add or remove handlers
            for (const Entry& e : snapshot) {
                if (std::find(m_Removed.begin(), m_Removed.end(), e.token) != m_Removed.end()) continue;
                const bool wasCancelled = hook.cancelled;
                e.fn(hook);
                if (!wasCancelled && hook.cancelled) hook.cancelledBy = e.owner;
            }
            --m_Depth;
            if (m_Depth == 0) m_Removed.clear();
            return !hook.cancelled;
        }

        bool Remove(Token token)
        {
            bool found = false;
            for (auto& kv : m_Hooks) {
                auto& list = kv.second;
                auto it = std::remove_if(list.begin(), list.end(), [&](const Entry& e) { return e.token == token; });
                if (it != list.end()) { list.erase(it, list.end()); found = true; }
            }
            if (found && m_Depth > 0) m_Removed.push_back(token);
            return found;
        }

        size_t RemoveOwner(PluginOwner owner)
        {
            size_t count = 0;
            for (auto& kv : m_Hooks) {
                auto& list = kv.second;
                for (const Entry& e : list)
                    if (e.owner == owner) { ++count; if (m_Depth > 0) m_Removed.push_back(e.token); }
                list.erase(std::remove_if(list.begin(), list.end(), [&](const Entry& e) { return e.owner == owner; }), list.end());
            }
            return count;
        }

        size_t Count() const
        {
            size_t n = 0;
            for (const auto& kv : m_Hooks) n += kv.second.size();
            return n;
        }

    private:
        struct Entry {
            Token token = 0;
            int priority = 0;
            PluginOwner owner = 0;
            std::function<void(HookBase&)> fn;
        };
        std::unordered_map<std::string, std::vector<Entry>> m_Hooks;
        std::vector<Token> m_Removed;
        Token m_Next = 0;
        int m_Depth = 0;
    };
}
