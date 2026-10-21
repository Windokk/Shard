// Shard C++ SDK : services between plugins (shard_services.h). A service is a C table of function pointers that starts with
// `uint32_t structSize`, published in a header by its provider :
//
//   struct WeatherApi { uint32_t structSize; float (SHARD_CALL *rain)(void* self, float x, float z); };
//   ctx.Services().Provide("weather.query", 1, 0, &kWeatherApi, this);                 // the provider
//   auto weather = ctx.Services().Get<WeatherApi>("weather.query", "^1");              // a user, in OnInitialize
//   if (auto w = weather.Resolve()) w.table->rain(w.self, 0.f, 0.f);                   // before each use
#pragma once

#include <string>

#include <shard/shard_services.h>
#include <shard/cpp/detail.hpp>

namespace shard {

    /// A service resolved now : its table and the provider's `self`. Do not keep it : resolve again before each use.
    template <typename Table>
    struct Resolved {
        const Table* table = nullptr;
        void* self = nullptr;
        uint32_t major = 0, minor = 0;
        explicit operator bool() const { return table != nullptr; }
    };

    /// A handle on a service : empty once its provider is disabled or unloaded
    template <typename Table>
    class Service {
    public:
        Service() = default;
        Service(ShardPluginCtx ctx, ShardServiceRef ref) : m_Ctx(ctx), m_Ref(ref) {}

        Resolved<Table> Resolve() const
        {
            Resolved<Table> out;
            const auto* api = detail::Interface<ShardServicesApi>(SHARD_IFACE_SERVICES);
            if (!api || !m_Ref) return out;
            ShardService service{};
            service.structSize = sizeof(ShardService);
            if (api->resolve(m_Ctx, m_Ref, &service) != SHARD_OK) return out;
            // A table older than the one this plugin was built with : the fields past its size are not there
            if (!service.table || *static_cast<const uint32_t*>(service.table) < sizeof(uint32_t)) return out;
            out.table = static_cast<const Table*>(service.table);
            out.self = service.self;
            out.major = service.major;
            out.minor = service.minor;
            return out;
        }
        explicit operator bool() const { return static_cast<bool>(Resolve()); }

    private:
        ShardPluginCtx m_Ctx = 0;
        ShardServiceRef m_Ref = 0;
    };

    class Services {
    public:
        explicit Services(ShardPluginCtx ctx) : m_Ctx(ctx) {}

        /// `table` stays valid until the plugin is disabled (a static table). Throws shard::Error if another plugin provides the name.
        template <typename Table>
        void Provide(const std::string& name, uint32_t major, uint32_t minor, const Table* table, void* self) const
        {
            detail::Check(Api().provide(m_Ctx, Str(name), major, minor, table, self), "service '" + name + "'");
        }

        /// Empty if nobody provides it in that range ("^1.2", ">=1", "*")
        template <typename Table>
        Service<Table> Get(const std::string& name, const std::string& range = "*") const
        {
            ShardServiceRef ref = 0;
            if (Api().get(m_Ctx, Str(name), Str(range), &ref) != SHARD_OK) return Service<Table>();
            return Service<Table>(m_Ctx, ref);
        }

    private:
        static const ShardServicesApi& Api() { return detail::Require<ShardServicesApi>(SHARD_IFACE_SERVICES); }
        ShardPluginCtx m_Ctx;
    };
}
