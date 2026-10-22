// Shard C++ SDK : scripts, the gameplay classes of a game (shard_scripts.h).
//
//   // health.hpp
//   class Health : public shard::Script {
//   public:
//       float maxHealth = 100.f;                    // fields: shown in the inspector, saved with the world
//       float current = 100.f;
//       shard::Text label;
//
//       void OnPlay() override { current = maxHealth; }
//       void OnTick(float dt) override { if (current <= 0.f) Owner().Destroy(); }
//       void Damage(float amount) { current -= amount; }
//   };
//
//   // health.cpp
//   #include "health.hpp"
//   SHARD_REGISTER_SCRIPT(Health, (maxHealth)(current)(label))
//
// Put as many scripts as you like in as many files as you like: each registers itself (shard/cpp/registry.hpp). In the editor the script is a
// component, "<plugin id>.Health", that you add to an actor like any other: its object is created with the component, its callbacks run in play
// mode in the same order as the engine's own scripts (the order the components were added), and it goes with the actor.
//
// The class is a normal C++ class: any member that is not listed stays out of the inspector and the world file, and starts fresh (a copy of the
// actor for play mode builds a new object and copies the listed fields into it). Keep the constructor free of calls into the engine (the SDK builds
// one to learn where the fields are): do that work in OnCreate or OnPlay.
#pragma once

#include <new>
#include <string>
#include <type_traits>
#include <utility>

#include <shard/shard_scripts.h>
#include <shard/cpp/actor.hpp>
#include <shard/cpp/component.hpp>
#include <shard/cpp/detail.hpp>
#include <shard/cpp/field.hpp>
#include <shard/cpp/registry.hpp>

namespace shard {

    /// A contact of the actor's body with another one (physics). The actor needs a "physics_body" component.
    struct Contact {
        Actor other;            ///< the other body's actor
        Vec3 normal;            ///< points from this actor toward the other one
        Vec3 point;             ///< a point of the contact, in the world
        float penetration = 0.f;    ///< how deep the bodies overlap
    };

    /// The base of a gameplay class. Override what you need; the host calls them as it calls the engine's own scripts.
    class Script {
    public:
        virtual ~Script() = default;

        /// The script joined an actor that is in a world (a component added in play mode, a world loaded, a spawn)
        virtual void OnCreate() {}
        /// The world finished loading
        virtual void OnWorldLoaded() {}
        virtual void OnWorldUnloaded() {}
        /// Play mode starts (the first thing a script of the world does)
        virtual void OnPlay() {}
        /// Every frame; `deltaTime` is the frame's, in seconds
        virtual void OnTick(float /*deltaTime*/) {}
        /// Every fixed simulation step, right before the physics step: forces and anything that must be reproducible go here
        virtual void OnFixedTick(float /*fixedDeltaTime*/) {}
        /// Every frame, after the scripts and the physics moved everything: cameras and whatever follows what moved
        virtual void OnLateTick(float /*deltaTime*/) {}
        /// Play mode stops
        virtual void OnStop() {}
        /// The script or its actor is destroyed
        virtual void OnDestroyed() {}
        virtual void OnActivated() {}
        virtual void OnDeactivated() {}
        /// The physics step found this actor's body touching another one, still touching it, or no longer touching it (OnContactEnded only
        /// knows `other`). They run right after the step, so the bodies can be read and changed.
        virtual void OnContactAdded(const Contact&) {}
        virtual void OnContactPersisted(const Contact&) {}
        virtual void OnContactEnded(const Contact&) {}

        /// The actor this script is on
        const Actor& Owner() const { return m_Owner; }
        /// The world it is in
        Scene GetScene() const { return m_Owner.GetScene(); }

    private:
        template <typename T> friend struct ScriptThunks;
        Actor m_Owner;
    };

    /// Which members of T are its fields : SHARD_REGISTER_SCRIPT specializes it
    template <typename T>
    struct ScriptTraits {
        static constexpr bool kDescribed = false;
    };

    /// The static functions the host calls, for a script class T
    template <typename T>
    struct ScriptThunks {
        static ShardResult Construct(void*, void* storage)
        {
            return detail::Guard(detail::PluginHandle(), "a script's constructor", [&] { ::new (storage) T(); });
        }
        static void SHARD_CALL Destruct(void*, void* storage)
        {
            try { static_cast<T*>(storage)->~T(); } catch (...) {}
        }
        static ShardResult Attach(void*, void* object, ShardActor owner)
        {
            static_cast<Script*>(static_cast<T*>(object))->m_Owner = Actor(owner);
            return SHARD_OK;
        }
        template <void (Script::*Fn)()>
        static ShardResult Event(void*, void* object)
        {
            return detail::Guard(detail::PluginHandle(), "a script", [&] { (static_cast<Script*>(static_cast<T*>(object))->*Fn)(); });
        }
        template <void (Script::*Fn)(const Contact&)>
        static ShardResult ContactEvent(void*, void* object, const ShardContact* c)
        {
            return detail::Guard(detail::PluginHandle(), "a script", [&] {
                Contact contact;
                contact.other = Actor(c->other);
                contact.normal = detail::FromArray(c->normal);
                contact.point = detail::FromArray(c->point);
                contact.penetration = c->penetration;
                (static_cast<Script*>(static_cast<T*>(object))->*Fn)(contact);
            });
        }
        // The text of a std::string member, for the host (SHARD_FIELD_ACCESSOR) : the offset is where the field is in the object
        static void SHARD_CALL TextGet(void*, void* object, uint32_t offset, ShardStr* out)
        {
            const std::string* s = reinterpret_cast<const std::string*>(static_cast<const char*>(object) + offset);
            *out = ShardStr{ s->data(), uint64_t(s->size()) };
        }
        static ShardResult TextSet(void*, void* object, uint32_t offset, ShardStr text)
        {
            return detail::Guard(detail::PluginHandle(), "a script's text", [&] {
                reinterpret_cast<std::string*>(static_cast<char*>(object) + offset)->assign(text.data ? text.data : "", size_t(text.size));
            });
        }
        template <void (Script::*Fn)(float)>
        static ShardResult Tick(void*, void* object, float dt)
        {
            return detail::Guard(detail::PluginHandle(), "a script", [&] { (static_cast<Script*>(static_cast<T*>(object))->*Fn)(dt); });
        }

        static const ShardScriptCallbacks& Table()
        {
            static const ShardScriptCallbacks table = [] {
                ShardScriptCallbacks c{};
                c.structSize = sizeof(ShardScriptCallbacks);
                c.construct = &Construct;
                c.destruct = &Destruct;
                c.attach = &Attach;
                c.on_create = &Event<&Script::OnCreate>;
                c.on_world_loaded = &Event<&Script::OnWorldLoaded>;
                c.on_world_unloaded = &Event<&Script::OnWorldUnloaded>;
                c.on_play = &Event<&Script::OnPlay>;
                c.on_tick = &Tick<&Script::OnTick>;
                c.on_fixed_tick = &Tick<&Script::OnFixedTick>;
                c.on_late_tick = &Tick<&Script::OnLateTick>;
                c.on_stop = &Event<&Script::OnStop>;
                c.on_destroyed = &Event<&Script::OnDestroyed>;
                c.on_activated = &Event<&Script::OnActivated>;
                c.on_deactivated = &Event<&Script::OnDeactivated>;
                c.on_contact_added = &ContactEvent<&Script::OnContactAdded>;
                c.on_contact_persisted = &ContactEvent<&Script::OnContactPersisted>;
                c.on_contact_ended = &ContactEvent<&Script::OnContactEnded>;
                c.text_get = &TextGet;
                c.text_set = &TextSet;
                return c;
            }();
            return table;
        }
    };

    class Scripts {
    public:
        explicit Scripts(ShardPluginCtx ctx) : m_Ctx(ctx) {}

        /// Registers the class T as "<plugin id>.<name>" (from OnLoad: SHARD_REGISTER_SCRIPT does it). Throws shard::Error if the host refuses it.
        template <typename T>
        void Register(const std::string& name, std::initializer_list<FieldOptions> options = {})
        {
            static_assert(std::is_base_of<Script, T>::value, "a script derives from shard::Script");
            static_assert(std::is_default_constructible<T>::value, "a script is default constructible (the host builds it)");
            static_assert(ScriptTraits<T>::kDescribed, "register this script with SHARD_REGISTER_SCRIPT");
            static_assert(sizeof(T) <= SHARD_SCRIPT_MAX_SIZE, "a script is 16 KB at most : keep its bulk in a shard::Blob or a heap allocation");
            static_assert(alignof(T) <= 16, "a script is aligned on 16 bytes at most");
            // A sample object tells where the fields are in the class : the members listed by the registration macro are found by their
            // address, the ones declared with SHARD_FIELD announce themselves while the sample is built
            alignas(T) unsigned char raw[sizeof(T)];
            detail::FieldCollector declared(reinterpret_cast<uintptr_t>(&raw[0]), sizeof(T));
            T* sample = nullptr;
            {
                detail::FieldCollectScope scope(declared);
                sample = ::new (static_cast<void*>(raw)) T();
            }
            std::vector<ShardFieldDesc> fields;
            try {
                fields = detail::FieldsOf<T, ScriptTraits<T>>(*sample, options);
                detail::AppendDeclaredFields(declared, fields);
            } catch (...) {
                sample->~T();
                throw;
            }
            sample->~T();
            ShardScriptDesc desc{};
            desc.structSize = sizeof(ShardScriptDesc);
            desc.size = uint32_t(sizeof(T));
            desc.align = uint32_t(alignof(T));
            desc.fieldCount = uint32_t(fields.size());
            desc.name = Str(name);
            desc.fields = fields.data();
            desc.user = nullptr;
            desc.callbacks = &ScriptThunks<T>::Table();
            detail::Check(detail::ScriptsApi().register_script(m_Ctx, &desc), "script '" + name + "'");
            detail::ScriptNames()[std::type_index(typeid(T))] = name;
        }

    private:
        ShardPluginCtx m_Ctx;
    };
}

/// Registers a script class under its class name. At global scope, in a source file of the plugin (it needs <shard/game.hpp>).
/// The fields are the members the class declares with SHARD_FIELD (shard/cpp/field.hpp), each with its own options, in the header :
///   SHARD_REGISTER_SCRIPT(Health)
/// The older way lists the members here instead (any field type of shard/cpp/component.hpp : numbers, bool, Vec2/3/4, Quat, Color, Text, AssetId,
/// Blob), and the two may be mixed :
///   SHARD_REGISTER_SCRIPT(Health, (maxHealth)(current))
/// SHARD_REGISTER_SCRIPT_EX adds the options of the listed fields (shard::FieldOptions: bounds, tooltip, SHARD_FIELD_RUNTIME), in parentheses:
///   SHARD_REGISTER_SCRIPT_EX(Health, (maxHealth)(current), ({ "maxHealth", 0, 0.f, 1000.f, "hit points" }))
#define SHARD_DETAIL_UNPAREN(...) __VA_ARGS__
#define SHARD_DETAIL_SCRIPT(Class, Name, sequence, optionsList)                                                       \
    template <> struct shard::ScriptTraits<Class> {                                                                   \
    static constexpr bool kDescribed = true;                                                                          \
    static void Describe(::shard::FieldList<Class>& fields)                                                           \
    {                                                                                                                 \
    using Self = Class;                                                                                               \
    SHARD_DETAIL_CAT(SHARD_DETAIL_FIELD_A sequence, _END)                                                             \
    }                                                                                                                 \
    };                                                                                                                \
    static ::shard::detail::Registrar SHARD_DETAIL_REG_CAT(shardScriptRegistrar_, __COUNTER__)(                       \
    "1script:" Name, [](::shard::Context& ctx) { ctx.Scripts().Register<Class>(Name, { optionsList }); });             

#define SHARD_DETAIL_SCRIPT_LISTED(Class, sequence) SHARD_DETAIL_SCRIPT(Class, #Class, sequence, )
/// A script whose fields are declared in the class (SHARD_FIELD), or with no fields at all (it only reacts : OnTick, contacts...)
#define SHARD_DETAIL_SCRIPT_DECLARED(Class)                                                                            \
    template <> struct shard::ScriptTraits<Class> {                                                                    \
        static constexpr bool kDescribed = true;                                                                       \
        static void Describe(::shard::FieldList<Class>&) {}                                                            \
    };                                                                                                                 \
    static ::shard::detail::Registrar SHARD_DETAIL_REG_CAT(shardScriptRegistrar_, __COUNTER__)(                        \
        "1script:" #Class, [](::shard::Context& ctx) { ctx.Scripts().Register<Class>(#Class); });

// SHARD_REGISTER_SCRIPT(Class) or SHARD_REGISTER_SCRIPT(Class, (a)(b)) : the number of arguments picks the form
#define SHARD_DETAIL_EXPAND(x) x
#define SHARD_DETAIL_PICK_SCRIPT(first, second, form, ...) form
#define SHARD_REGISTER_SCRIPT(...) \
    SHARD_DETAIL_EXPAND(SHARD_DETAIL_PICK_SCRIPT(__VA_ARGS__, SHARD_DETAIL_SCRIPT_LISTED, SHARD_DETAIL_SCRIPT_DECLARED, unused)(__VA_ARGS__))
#define SHARD_REGISTER_SCRIPT_EX(Class, sequence, options) SHARD_DETAIL_SCRIPT(Class, #Class, sequence, SHARD_DETAIL_UNPAREN options)
#define SHARD_REGISTER_SCRIPT_BARE(Class) SHARD_DETAIL_SCRIPT_DECLARED(Class)

/// A component of plain data (the ECS kind: systems query it, shard/cpp/system.hpp), defined and registered from any file; found anywhere after
/// OnLoad with shard::ComponentOf<Type>().
///   struct Velocity { float x = 0.f, y = 0.f, z = 0.f; };
///   SHARD_DEFINE_COMPONENT(Velocity, (x)(y)(z))
namespace shard {
    template <typename T> ComponentType<T>& ComponentOf()
    {
        static ComponentType<T> type;
        return type;
    }
}
#define SHARD_DEFINE_COMPONENT(Type, sequence)                                                                      \
    SHARD_COMPONENT(Type, sequence)                                                                                 \
    static ::shard::detail::Registrar SHARD_DETAIL_REG_CAT(shardComponentRegistrar_, __COUNTER__)(                  \
        "0component:" #Type, [](::shard::Context& ctx) { ::shard::ComponentOf<Type>() = ctx.Components().Register<Type>(#Type); });
