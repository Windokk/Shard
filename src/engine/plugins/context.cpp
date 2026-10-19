#include "engine/plugins/context.hpp"

#include <iostream>

#include "engine/core/diagnostics/logger.hpp"
#include "engine/world/components/registry/component_registration.hpp"
#include "engine/world/engine.hpp"

namespace Shard::Engine::Plugins {

    // ---- PluginContext ----

    PluginContext::PluginContext(const PluginManifest& manifest, PluginOwner owner, Host host, HostBindings bindings)
        : m_Manifest(&manifest), m_Owner(owner), m_Host(host), m_B(std::move(bindings)),
          m_Memory(m_B, uint64_t(manifest.budgetMemoryMB * 1024.0 * 1024.0), manifest.enforceBudgets)
    {
    }

    PluginContext::~PluginContext() { ReleaseAll(); }

    // ---- PluginMemory ----

    namespace {
        // The size goes in front of the block, in a header as big as the alignment asks (16 at least)
        size_t HeaderSize(size_t align) { return align < 16 ? 16 : align; }
    }

    void* PluginMemory::Allocate(size_t size, size_t align)
    {
        if (align == 0 || (align & (align - 1)) != 0) return nullptr;
        const size_t header = HeaderSize(align);
        const uint64_t used = m_Used.load(std::memory_order_relaxed);
        if (m_Limit != 0 && used + size > m_Limit) {
            m_WentOver.store(true, std::memory_order_relaxed);
            if (m_Enforce) {
                m_Failures.fetch_add(1, std::memory_order_relaxed);
                return nullptr;
            }
        }
        const size_t total = header + size;
        unsigned char* raw = static_cast<unsigned char*>(m_Allocate ? m_Allocate(total, header) : ::operator new(total, std::align_val_t(header)));
        if (!raw) return nullptr;
        // [ ... size | align ][ block ]
        reinterpret_cast<uint64_t*>(raw + header - 16)[0] = size;
        reinterpret_cast<uint64_t*>(raw + header - 16)[1] = align;
        const uint64_t now = m_Used.fetch_add(size, std::memory_order_relaxed) + size;
        uint64_t peak = m_Peak.load(std::memory_order_relaxed);
        while (now > peak && !m_Peak.compare_exchange_weak(peak, now, std::memory_order_relaxed)) {}
        m_Count.fetch_add(1, std::memory_order_relaxed);
        return raw + header;
    }

    void PluginMemory::Free(void* pointer)
    {
        if (!pointer) return;
        unsigned char* block = static_cast<unsigned char*>(pointer);
        // Allocate left the size and the alignment in the 16 bytes before the block
        const uint64_t size = reinterpret_cast<const uint64_t*>(block - 16)[0];
        const size_t header = HeaderSize(size_t(reinterpret_cast<const uint64_t*>(block - 16)[1]));
        unsigned char* raw = block - header;
        m_Used.fetch_sub(size, std::memory_order_relaxed);
        if (m_Deallocate) m_Deallocate(raw, size_t(header + size), header);
        else ::operator delete(raw, std::align_val_t(header));
    }

    void PluginContext::AddRecord(std::string kind, std::string name, std::function<void()> release)
    {
        m_Records.push_back(Record{ std::move(kind), std::move(name), std::move(release) });
    }

    bool PluginContext::Owns(const std::string& kind) const
    {
        if (m_Released) return false;
        for (const Record& r : m_Records) if (r.kind == kind) return true;
        return false;
    }

    void PluginContext::ReleaseAll()
    {
        if (m_Released) return;
        m_Released = true;
        // Last registered, first removed : what was built on something goes before it
        for (auto it = m_Records.rbegin(); it != m_Records.rend(); ++it) {
            try {
                if (it->release) it->release();
            } catch (const std::exception& e) {
                std::cerr << "[PLUGINS] " << Id() << " : releasing " << it->kind << " '" << it->name << "' threw : " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[PLUGINS] " << Id() << " : releasing " << it->kind << " '" << it->name << "' threw" << std::endl;
            }
        }
        m_Records.clear();
        m_SystemNames.clear();
        m_PendingSystems.clear();
        m_PendingEvents.clear();
    }

    std::vector<std::pair<std::string, size_t>> PluginContext::Summary() const
    {
        std::vector<std::pair<std::string, size_t>> out;
        for (const Record& r : m_Records) {
            auto it = std::find_if(out.begin(), out.end(), [&](const auto& p) { return p.first == r.kind; });
            if (it == out.end()) out.push_back({ r.kind, 1 });
            else ++it->second;
        }
        for (const auto& p : m_PendingSystems) {
            (void)p;
            auto it = std::find_if(out.begin(), out.end(), [](const auto& q) { return q.first == "system"; });
            if (it == out.end()) out.push_back({ "system", 1 }); else ++it->second;
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    void PluginContext::Log(const std::string& message) const
    {
        if (m_B.logger) m_B.logger->Log(Debugging::Level::Info, "plugin", 0, "[" + Id() + "] " + message);
        else std::cout << "[" << Id() << "] " << message << std::endl;
    }

    void PluginContext::Warn(const std::string& message) const
    {
        if (m_B.logger) m_B.logger->Log(Debugging::Level::Warning, "plugin", 0, "[" + Id() + "] " + message);
        else std::cerr << "[" << Id() << "] WARNING " << message << std::endl;
    }

    void PluginContext::Error(const std::string& message) const
    {
        if (m_B.logger) m_B.logger->Log(Debugging::Level::Error, "plugin", 0, "[" + Id() + "] " + message);
        else std::cerr << "[" << Id() << "] ERROR " << message << std::endl;
    }

    Core::Random PluginContext::MakeRandomStream(std::string_view name) const
    {
        const std::string full = Id() + "/" + std::string(name);
        if (m_B.engine) return m_B.engine->MakeRandomStream(full);
        return Core::Random(Core::Fnv1a64(full));
    }

    bool PluginContext::Has(const std::string& permission) const
    {
        const auto& p = m_Manifest->permissions;
        return std::find(p.begin(), p.end(), permission) != p.end();
    }

    void PluginContext::BindEngine(Core::Ecs::Scheduler* scheduler, Events::EventDispatcher* events)
    {
        m_B.scheduler = scheduler;
        m_B.events = events;
        if (scheduler) {
            for (auto& [name, desc] : m_PendingSystems) {
                if (scheduler->Add(desc) == Core::Ecs::Scheduler::kInvalid) {
                    Warn("system '" + name + "' was refused by the scheduler (name taken, or no function)");
                    continue;
                }
                Core::Ecs::Scheduler* sched = scheduler;
                const std::string full = name;
                AddRecord("system", full, [sched, full]() { sched->Remove(full); });
                m_SystemNames.push_back(full);
            }
            m_PendingSystems.clear();
        }
        if (events && !m_PendingEvents.empty()) {
            const bool first = std::none_of(m_Records.begin(), m_Records.end(), [](const Record& r) { return r.kind == "events"; });
            for (auto& [name, subscribe] : m_PendingEvents) subscribe(*events, m_Owner);
            m_PendingEvents.clear();
            if (first) {
                Events::EventDispatcher* d = events;
                const PluginOwner owner = m_Owner;
                AddRecord("events", "subscriptions", [d, owner]() { d->UnsubscribeOwner(owner); });
            }
        }
    }

    // ---- ComponentRegistrar ----

    bool ComponentRegistrar::Register(const std::string& name, Objects::Components::ComponentFactory factory)
    {
        auto* registry = m_Ctx.m_B.components;
        if (!registry || !factory) return false;
        if (registry->IsRegistered(name)) {
            m_Ctx.Warn("component '" + name + "' is already registered");
            return false;
        }
        registry->RegisterComponentType(name, factory);
        m_Ctx.AddRecord("component", name, [registry, name]() { registry->UnregisterComponentType(name); });
        return true;
    }

    std::vector<std::string> ComponentRegistrar::Snapshot() const
    {
        std::vector<std::string> names;
        if (auto* registry = m_Ctx.m_B.components)
            for (const auto& kv : registry->GetAll()) names.push_back(kv.first);
        std::sort(names.begin(), names.end());
        return names;
    }

    size_t ComponentRegistrar::AdoptNewSince(const std::vector<std::string>& before)
    {
        auto* registry = m_Ctx.m_B.components;
        if (!registry) return 0;
        size_t added = 0;
        for (const std::string& name : Snapshot()) {
            if (std::binary_search(before.begin(), before.end(), name)) continue;
            m_Ctx.AddRecord("component", name, [registry, name]() { registry->UnregisterComponentType(name); });
            ++added;
        }
        return added;
    }

    size_t ComponentRegistrar::AdoptStaticRegistrars()
    {
        auto* registry = m_Ctx.m_B.components;
        if (!registry) return 0;
        const std::vector<std::string> before = Snapshot();
        for (auto& registrar : Objects::Components::GetComponentRegistrars())
            registrar(*registry);
        return AdoptNewSince(before);
    }

    // ---- SystemRegistrar ----

    std::string SystemRegistrar::NameOf(const std::string& name) const { return m_Ctx.Id() + "/" + name; }

    std::string SystemRegistrar::Add(Core::Ecs::SystemDesc desc, Core::Ecs::SystemFn fn)
    {
        desc.run = std::move(fn);
        return Add(std::move(desc));
    }

    std::string SystemRegistrar::Add(Core::Ecs::SystemDesc desc)
    {
        const std::string local = desc.name;
        desc.name = NameOf(local);
        auto resolve = [this](std::vector<std::string>& names) {
            for (std::string& n : names)
                if (n.compare(0, 2, "./") == 0) n = NameOf(n.substr(2));
        };
        resolve(desc.after);
        resolve(desc.before);

        if (!desc.run) {
            m_Ctx.Warn("system '" + local + "' has no function");
            return {};
        }
        // Every call into the plugin's system names the plugin (for a crash report) and keeps an exception from reaching the scheduler
        {
            Core::Ecs::SystemFn inner = std::move(desc.run);
            std::atomic<uint32_t>* active = m_Ctx.m_B.activePlugin;
            auto report = m_Ctx.m_B.reportError;
            const PluginOwner owner = m_Ctx.m_Owner;
            desc.run = [inner = std::move(inner), active, report, owner](Core::Ecs::SystemContext& sc) {
                if (active) active->store(owner, std::memory_order_relaxed);
                try {
                    inner(sc);
                } catch (const std::exception& e) {
                    if (report) report(owner, e.what());
                } catch (...) {
                    if (report) report(owner, "unknown exception");
                }
            };
        }
        const std::string full = desc.name;
        if (m_Ctx.m_B.scheduler) {
            Core::Ecs::Scheduler* scheduler = m_Ctx.m_B.scheduler;
            if (scheduler->Add(std::move(desc)) == Core::Ecs::Scheduler::kInvalid) {
                m_Ctx.Warn("system '" + full + "' was refused by the scheduler (name taken)");
                return {};
            }
            m_Ctx.AddRecord("system", full, [scheduler, full]() { scheduler->Remove(full); });
            m_Ctx.m_SystemNames.push_back(full);
        } else {
            m_Ctx.m_PendingSystems.emplace_back(full, std::move(desc));
        }
        return full;
    }

    bool SystemRegistrar::SetEnabled(const std::string& name, bool enabled)
    {
        if (!m_Ctx.m_B.scheduler) return false;
        return m_Ctx.m_B.scheduler->SetEnabled(name.find('/') == std::string::npos ? NameOf(name) : name, enabled);
    }

    // ---- EventRegistrar ----

    Events::EventDispatcher* EventRegistrar::Dispatcher() const { return m_Ctx.m_B.events; }

    void EventRegistrar::Defer(std::string name, std::function<void(Events::EventDispatcher&, Events::EventDispatcher::Owner)> subscribe)
    {
        if (Events::EventDispatcher* d = m_Ctx.m_B.events) {
            subscribe(*d, m_Ctx.m_Owner);
            const bool first = std::none_of(m_Ctx.m_Records.begin(), m_Ctx.m_Records.end(), [](const PluginContext::Record& r) { return r.kind == "events"; });
            if (first) {
                const PluginOwner owner = m_Ctx.m_Owner;
                m_Ctx.AddRecord("events", "subscriptions", [d, owner]() { d->UnsubscribeOwner(owner); });
            }
        } else {
            m_Ctx.m_PendingEvents.emplace_back(std::move(name), std::move(subscribe));
        }
    }

    // ---- CommandRegistrar / CVarRegistrar ----

    bool CommandRegistrar::Add(const std::string& name, const std::string& help, Core::CVarRegistry::CommandFn fn)
    {
        Core::CVarRegistry* registry = m_Ctx.m_B.cvars;
        if (!registry || !fn) return false;
        registry->RegisterCommand(name, help, std::move(fn));
        m_Ctx.AddRecord("command", name, [registry, name]() { registry->UnregisterCommand(name); });
        return true;
    }

    std::string CVarRegistrar::NameOf(const std::string& name) const { return m_Ctx.Id() + "." + name; }

    Core::CVar<bool>* CVarRegistrar::Bool(const std::string& name, bool value, const std::string& help, Core::CVarFlags flags)
    {
        return Make<bool>(name, value, help, flags);
    }

    Core::CVar<int>* CVarRegistrar::Int(const std::string& name, int value, const std::string& help, Core::CVarFlags flags, int min, int max)
    {
        return Make<int>(name, value, help, flags, min, max);
    }

    Core::CVar<float>* CVarRegistrar::Float(const std::string& name, float value, const std::string& help, Core::CVarFlags flags, float min, float max)
    {
        return Make<float>(name, value, help, flags, min, max);
    }

    Core::CVar<std::string>* CVarRegistrar::String(const std::string& name, const std::string& value, const std::string& help, Core::CVarFlags flags)
    {
        return Make<std::string>(name, value, help, flags);
    }

    // ---- WorldRegistrar / AssetRegistrar ----

    bool WorldRegistrar::AddExtension(std::type_index type, Worlds::WorldExtensionFactory factory)
    {
        if (!m_Ctx.m_B.registerWorldExtension) return false;
        if (!m_Ctx.m_B.registerWorldExtension(type, std::move(factory))) {
            m_Ctx.Warn("a world extension of the same type is already registered");
            return false;
        }
        auto unregister = m_Ctx.m_B.unregisterWorldExtension;
        m_Ctx.AddRecord("world-extension", type.name(), [unregister, type]() { if (unregister) unregister(type); });
        return true;
    }

    void WorldRegistrar::RegisterSettingsAssetRefs(Worlds::WorldSettingsAssetRefs refs)
    {
        if (!m_Ctx.m_B.registerSettingsRefs) return;
        const uint32_t id = m_Ctx.m_B.registerSettingsRefs(std::move(refs));
        auto unregister = m_Ctx.m_B.unregisterSettingsRefs;
        m_Ctx.AddRecord("world-settings-refs", std::to_string(id), [unregister, id]() { if (unregister) unregister(id); });
    }

    std::string AssetRegistrar::Uri(const std::string& path) const { return ContentMounts::MakeModUri(m_Ctx.Id(), path); }

    Core::Guid AssetRegistrar::GuidOf(const std::string& path) const { return ContentMounts::AssetGuid(m_Ctx.Manifest().Namespace(), path); }

    bool ServiceRegistrar::Has(const std::string& name) const
    {
        return m_Ctx.m_B.services && m_Ctx.m_B.services->Has(name);
    }
}
