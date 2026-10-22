// Shard C++ SDK : registrations that live where the code lives.
//
// A plugin of one file registers everything in OnLoad. A game does not: each .cpp defines its own scripts, components and systems and says so
// at the bottom of the file, with one of these macros. They add themselves to a list when the library is loaded, and the SDK runs the list at the
// start of OnLoad, in a fixed order (by kind, then by name - never by the order the compiler happened to link the files, which would change the ids
// of the components and the order of the systems from one build to the next).
//
//   SHARD_REGISTER_SCRIPT(Health, (maxHealth)(current))                   // shard/cpp/script.hpp
//   SHARD_DEFINE_COMPONENT(Velocity, (x)(y)(z))                           // a component of plain data, found anywhere with shard::ComponentOf<Velocity>()
//   SHARD_ON_LOAD(Movement, ctx) { ctx.Systems().Add(...).Run(...); }     // anything else: systems, cvars, commands, hooks, events...
//
// The macros go at global scope, in a source file of the plugin's library (not in a static library the plugin links: the linker may drop a file
// nothing references).
#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace shard {

    class Context;

    namespace detail {

        using RegistrationFn = void (*)(Context&);

        struct Registration {
            std::string key;
            RegistrationFn fn;
        };

        inline std::vector<Registration>& Registrations()
        {
            static std::vector<Registration> list;
            return list;
        }

        /// Adds itself to the list at static initialization. `key` : "<phase digit><kind>:<name>", the order the list runs in.
        struct Registrar {
            Registrar(std::string key, RegistrationFn fn) { Registrations().push_back(Registration{ std::move(key), fn }); }
        };

        /// Runs the list (the SDK does it at the start of OnLoad)
        inline void RunRegistrations(Context& ctx)
        {
            std::vector<Registration> list = Registrations();
            std::stable_sort(list.begin(), list.end(), [](const Registration& a, const Registration& b) { return a.key < b.key; });
            for (const Registration& r : list) r.fn(ctx);
        }
    }
}

#define SHARD_DETAIL_REG_CAT(a, b) SHARD_DETAIL_REG_CAT_(a, b)
#define SHARD_DETAIL_REG_CAT_(a, b) a##b

/// The body that follows runs in OnLoad, after the components and the scripts are registered: `ctx` is the shard::Context. `Name` only orders
/// and names it (unique in the plugin, letters and digits).
#define SHARD_ON_LOAD(Name, ctx)                                                                                    \
    static void SHARD_DETAIL_REG_CAT(ShardOnLoad_, Name)(::shard::Context& ctx);                                    \
    static ::shard::detail::Registrar SHARD_DETAIL_REG_CAT(shardRegistrar_, Name)("3load:" #Name, &SHARD_DETAIL_REG_CAT(ShardOnLoad_, Name)); \
    static void SHARD_DETAIL_REG_CAT(ShardOnLoad_, Name)(::shard::Context& ctx)
