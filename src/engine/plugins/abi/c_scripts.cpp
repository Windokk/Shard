#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include <shard/shard_scripts.h>

#include "engine/plugins/abi/c_fields.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/components/script.hpp"
#include "engine/world/time_manager.hpp"

// shard.scripts : a script type of a plugin is a Script component of the engine (the same one the game's own C++ scripts derive from, so the
// world ticks it in the same order, play mode starts and stops it, and the inspector and the world file see its fields) whose object lives in
// storage the component holds, built and destroyed by the plugin's callbacks.

namespace Shard::Engine::Plugins::CAbi {

    struct CScriptType : FieldSet {
        std::string name;                       // full : "<plugin id>.<name>"
        std::string shortName;
        PluginOwner owner = 0;
        uint32_t size = 0;
        uint32_t align = 0;
        void* user = nullptr;
        ShardScriptCallbacks cb{};
        std::atomic<uint32_t>* active = nullptr;
        std::function<void(PluginOwner, const std::string&)> report;
        std::atomic<bool> alive{ true };        // false once the plugin's context released everything: its callbacks are not called any more
    };

    namespace {
        std::mutex gMutex;
        std::map<std::string, std::shared_ptr<CScriptType>> gTypes;     // by full name

        class CScriptBase : public Objects::Components::Script {
        public:
            explicit CScriptBase(std::shared_ptr<const CScriptType> type) : Script(nullptr, 0), m_Type(std::move(type))
            {
                // What the field helpers need from the script : its own strings, and the actors its fields refer to
                m_Hooks.user = this;
                m_Hooks.textGet = &TextGetThunk;
                m_Hooks.textSet = &TextSetThunk;
                m_Hooks.actorPath = [](ShardActor handle) { return ActorPath(handle); };
                m_Hooks.actorAt = [this](const std::string& path) { return ActorAtPath(parent, path); };
                for (const CField* f : m_Type->texts) m_HasActorFields = m_HasActorFields || f->actor;
            }

            static size_t TextOffset()
            {
#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
                return offsetof(CScriptBase, m_Text);
#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic pop
#endif
            }

            virtual unsigned char* Data() const = 0;
            const CScriptType& Type() const { return *m_Type; }
            const std::shared_ptr<const CScriptType>& TypePtr() const { return m_Type; }

            bool Construct()
            {
                bool built = false;
                Call("construct", [&] {
                    const ShardResult r = m_Type->cb.construct(m_Type->user, Data());
                    built = r == SHARD_OK;
                    return r;
                });
                m_Constructed = built;
                return built;
            }

            // ---- The callbacks of the engine's Script, into the plugin's ----

            void OnCreate() override { ResolveRefs(); Fire(m_Type->cb.on_create, "on_create"); }
            void OnWorldLoaded() override { ResolveRefs(); Fire(m_Type->cb.on_world_loaded, "on_world_loaded"); }
            void OnWorldUnloaded() override { Fire(m_Type->cb.on_world_unloaded, "on_world_unloaded"); }
            void OnPlay() override { ResolveRefs(); Fire(m_Type->cb.on_play, "on_play"); }
            void OnStop() override { Fire(m_Type->cb.on_stop, "on_stop"); }
            void OnActivated() override { Fire(m_Type->cb.on_activated, "on_activated"); }
            void OnDeactivated() override { Fire(m_Type->cb.on_deactivated, "on_deactivated"); }
            void OnTick() override { ResolveRefs(); Tick(m_Type->cb.on_tick, "on_tick", false); }
            void OnFixedTick() override { ResolveRefs(); Tick(m_Type->cb.on_fixed_tick, "on_fixed_tick", true); }
            void OnLateTick() override { Tick(m_Type->cb.on_late_tick, "on_late_tick", false); }

            // ---- Physics contacts ----

            void OnContactAdded(const Events::ContactAddedEvent& e) override { if (m_Type->cb.on_contact_added) Contact(m_Type->cb.on_contact_added, "on_contact_added", DecodeContact(e)); }
            void OnContactPersisted(const Events::ContactPersistedEvent& e) override { if (m_Type->cb.on_contact_persisted) Contact(m_Type->cb.on_contact_persisted, "on_contact_persisted", DecodeContact(e)); }
            void OnContactEnded(const Events::ContactRemovedEvent& e) override { if (m_Type->cb.on_contact_ended) Contact(m_Type->cb.on_contact_ended, "on_contact_ended", DecodeContact(e)); }

            void OnDestroyed() override
            {
                if (m_Destroyed) return;
                m_Destroyed = true;
                Fire(m_Type->cb.on_destroyed, "on_destroyed");
            }

            void Destroy() override
            {
                OnDestroyed();
                if (m_Constructed) FreeFieldBlobs(*m_Type, m_Type->owner, Data());
                Objects::Components::Component::Destroy();
            }

            void OnAttached() override
            {
                if (!m_Type->cb.attach || !parent) return;
                const ShardActor owner = ActorHandle(parent);
                Call("attach", [&] { return m_Type->cb.attach(m_Type->user, Data(), owner); });
            }

            // ---- What the world file and the inspector see ----

            const ClassDescriptor* GetDescriptor() const override
            {
                if (m_Constructed) PullTexts(*m_Type, m_Type->owner, Data(), m_Text, &m_Hooks);      // what the script put in its texts since
                return &m_Type->descriptor;
            }

            void Deserialize(const json componentData) override
            {
                DeserializeReflectedFields(componentData, false);
                if (m_Constructed) {
                    DeserializeBlobs(*m_Type, m_Type->owner, Data(), componentData);
                    PushTexts(*m_Type, m_Type->owner, Data(), m_Text, &m_Hooks);
                }
                DeserializeActive(componentData, true);
            }

            ordered_json Serialize() override
            {
                if (m_Constructed) PullTexts(*m_Type, m_Type->owner, Data(), m_Text, &m_Hooks);
                ordered_json out = SerializeReflected(m_Type->name);
                if (m_Constructed) SerializeBlobs(*m_Type, m_Type->owner, Data(), out);
                return out;
            }

            void OnFieldChanged(const FieldChangedEvent&) override
            {
                if (m_Constructed) PushTexts(*m_Type, m_Type->owner, Data(), m_Text, &m_Hooks);
            }

            void Activate() override { Script::Activate(); }
            void DeActivate() override { Script::DeActivate(); }

        protected:
            // Pulls the texts the script wrote, for CopyFrom
            void PullAll() const { if (m_Constructed) PullTexts(*m_Type, m_Type->owner, Data(), m_Text, &m_Hooks); }
            void CopyFrom(const CScriptBase& other)
            {
                other.PullAll();
                CopyFields(*m_Type, m_Type->owner, Data(), m_Text, other.Data(), other.m_Text);
                // The plugin's own strings go into the new object ; an actor reference keeps its path until the copy is in a world and the
                // actor it names can be found there
                PushTexts(*m_Type, m_Type->owner, Data(), m_Text, &m_Hooks);
                activated = other.activated;
            }

            bool m_Constructed = false;

        private:
            void Report(const std::string& what) const
            {
                const std::string message = "script '" + m_Type->shortName + "' : " + what;
                if (m_Type->report) m_Type->report(m_Type->owner, message);
            }

            template <typename F>
            void Call(const char* what, F&& f) const
            {
                if (!m_Type->alive.load()) return;
                ActiveScope active(m_Type->active, m_Type->owner);
                ScriptScope scope;
                try {
                    const ShardResult r = f();
                    if (r != SHARD_OK) Report(std::string(what) + " returned " + std::to_string(r));
                } catch (const std::exception& e) {
                    Report(std::string(what) + " threw : " + e.what());
                } catch (...) {
                    Report(std::string(what) + " threw");
                }
            }

            // The plugin's text_get / text_set, entered like any other call into it
            static void TextGetThunk(void* self, void*, uint32_t offset, ShardStr* out) { static_cast<CScriptBase*>(self)->DoTextGet(offset, out); }
            static ShardResult TextSetThunk(void* self, void*, uint32_t offset, ShardStr text) { return static_cast<CScriptBase*>(self)->DoTextSet(offset, text); }

            void DoTextGet(uint32_t offset, ShardStr* out) const
            {
                *out = ShardStr{ nullptr, 0 };
                if (!m_Type->cb.text_get || !m_Type->alive.load()) return;
                ActiveScope active(m_Type->active, m_Type->owner);
                try { m_Type->cb.text_get(m_Type->user, Data(), offset, out); } catch (...) { *out = ShardStr{ nullptr, 0 }; }
            }

            ShardResult DoTextSet(uint32_t offset, ShardStr text)
            {
                if (!m_Type->cb.text_set || !m_Type->alive.load()) return SHARD_E_UNSUPPORTED;
                ActiveScope active(m_Type->active, m_Type->owner);
                try { return m_Type->cb.text_set(m_Type->user, Data(), offset, text); } catch (...) { return SHARD_E_INTERNAL; }
            }

            // The actors the fields refer to, once they exist
            void ResolveRefs()
            {
                if (!m_Constructed || !m_HasActorFields || !parent) return;
                bool pending = false;
                for (bool p : m_Hooks.pending) pending = pending || p;
                if (pending) ResolveActorFields(*m_Type, Data(), m_Text, m_Hooks);
            }

            void Contact(ShardScriptContactFn fn, const char* what, const ContactInfo& info)
            {
                if (!fn || !parent || !info.other) return;
                ShardContact contact{};
                contact.structSize = sizeof(ShardContact);
                contact.other = ActorHandle(info.other);
                if (info.hasManifold) {
                    glm::vec3 normal(info.normal[0], info.normal[1], info.normal[2]);
                    // Jolt's normal runs from the first body to the second : this one is either
                    const glm::vec3 toOther = info.other->transform->GetWorldPosition() - parent->transform->GetWorldPosition();
                    if (glm::dot(normal, toOther) < 0.0f) normal = -normal;
                    contact.normal[0] = normal.x; contact.normal[1] = normal.y; contact.normal[2] = normal.z;
                    if (info.hasPoint) {
                        contact.point[0] = info.point[0]; contact.point[1] = info.point[1]; contact.point[2] = info.point[2];
                    }
                    contact.penetration = info.penetration;
                }
                Call(what, [&] { return fn(m_Type->user, Data(), &contact); });
            }

            void Fire(ShardScriptFn fn, const char* what)
            {
                if (fn) Call(what, [&] { return fn(m_Type->user, Data()); });
            }

            void Tick(ShardScriptTickFn fn, const char* what, bool fixed)
            {
                if (!fn) return;
                Core::IEngineContext* engine = GetEngineContext();
                Time::TimeManager* time = engine ? engine->GetTimeManager() : nullptr;
                const float dt = time ? (fixed ? time->GetFixedDeltaTime() : time->GetDeltaTime()) : 0.0f;
                Call(what, [&] { return fn(m_Type->user, Data(), dt); });
            }

            std::shared_ptr<const CScriptType> m_Type;
            mutable std::string m_Text[SHARD_COMPONENT_MAX_STRINGS];        // the string and actor fields, as the inspector and the world file see them
            mutable FieldHooks m_Hooks;
            bool m_Destroyed = false;

        protected:
            bool m_HasActorFields = false;
        };

        template <size_t N>
        class CScriptComponent final : public CScriptBase {
        public:
            explicit CScriptComponent(std::shared_ptr<const CScriptType> type) : CScriptBase(std::move(type)) { std::memset(m_Storage, 0, N); }
            // The component's constructor is the one Object::Create hands (parent, local id, type): the parent comes with OnAttached
            CScriptComponent(std::shared_ptr<Objects::Actor>, uint32_t, std::shared_ptr<const CScriptType> type) : CScriptComponent(std::move(type)) {}

            ~CScriptComponent() override
            {
                if (m_Constructed && Type().alive.load()) {
                    ActiveScope active(Type().active, Type().owner);
                    try { Type().cb.destruct(Type().user, m_Storage); } catch (...) {}
                }
            }

            static size_t StorageOffset()
            {
#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
                return offsetof(CScriptComponent, m_Storage);
#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic pop
#endif
            }

            unsigned char* Data() const override { return m_Storage; }

            std::shared_ptr<Objects::Components::Component> Clone() const override;

        private:
            alignas(16) mutable unsigned char m_Storage[N];
        };

        template <size_t N>
        std::shared_ptr<Objects::Components::Component> MakeComponent(const std::shared_ptr<const CScriptType>& type)
        {
            auto component = Core::Object::Create<CScriptComponent<N>>(nullptr, 0, type);
            if (!component->Construct()) {
                component->Objects::Components::Component::Destroy();
                return nullptr;
            }
            return component;
        }

        // A copy of the actor (play mode): a new object, with the fields of this one in it
        template <size_t N>
        std::shared_ptr<Objects::Components::Component> CScriptComponent<N>::Clone() const
        {
            auto copy = std::static_pointer_cast<CScriptComponent<N>>(MakeComponent<N>(TypePtr()));
            if (copy) copy->CopyFrom(*this);
            return copy;
        }
    }

    namespace {
        ShardResult SHARD_CALL RegisterScript(ShardPluginCtx handle, const ShardScriptDesc* desc)
        {
            try {
                if (!desc) return SHARD_E_INVALID_ARG;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                auto refuse = [&](const std::string& why) { ctx->Error("script type : " + why); return SHARD_E_INVALID_ARG; };

                if (desc->structSize < sizeof(ShardScriptDesc)) return refuse("its ShardScriptDesc is too small");
                const std::string local = ToString(desc->name);
                if (!IsFieldName(local)) return refuse("'" + local + "' is not a script name (letters, digits and '_')");
                const std::string full = ctx->Id() + "." + local;
                if (desc->size == 0 || desc->size > SHARD_SCRIPT_MAX_SIZE) return refuse("'" + full + "' : a size of " + std::to_string(desc->size) + " bytes (1 to " + std::to_string(SHARD_SCRIPT_MAX_SIZE) + ")");
                if (desc->align == 0 || desc->align > 16 || (desc->align & (desc->align - 1)) != 0) return refuse("'" + full + "' : an alignment of " + std::to_string(desc->align));
                if (desc->fieldCount > SHARD_COMPONENT_MAX_FIELDS) return refuse("'" + full + "' has more than " + std::to_string(SHARD_COMPONENT_MAX_FIELDS) + " fields");
                if (desc->fieldCount && !desc->fields) return refuse("'" + full + "' : fieldCount without fields");
                if (!desc->callbacks || desc->callbacks->structSize < offsetof(ShardScriptCallbacks, attach)) return refuse("'" + full + "' : no callbacks table");
                if (!desc->callbacks->construct || !desc->callbacks->destruct) return refuse("'" + full + "' : construct and destruct are required");

                auto type = std::make_shared<CScriptType>();
                type->name = full;
                type->shortName = local;
                type->owner = PluginOwner(handle);
                type->size = desc->size;
                type->align = desc->align;
                type->user = desc->user;
                std::memcpy(&type->cb, desc->callbacks, std::min<size_t>(desc->callbacks->structSize, sizeof(ShardScriptCallbacks)));
                type->active = ActiveCounter(*ctx);
                type->report = ctx->Bindings().reportError;

                // The storage comes in the smallest size class that holds the object ; its place in the component is the same in all of them
                size_t storageOffset = 0;
                std::function<std::shared_ptr<Objects::Components::Component>(const std::shared_ptr<const CScriptType>&)> make;
                if (desc->size <= 256) { storageOffset = CScriptComponent<256>::StorageOffset(); make = &MakeComponent<256>; }
                else if (desc->size <= 1024) { storageOffset = CScriptComponent<1024>::StorageOffset(); make = &MakeComponent<1024>; }
                else if (desc->size <= 4096) { storageOffset = CScriptComponent<4096>::StorageOffset(); make = &MakeComponent<4096>; }
                else { storageOffset = CScriptComponent<16384>::StorageOffset(); make = &MakeComponent<16384>; }
                std::string why;
                if (!BuildFieldSet(*type, full, desc->fields, desc->fieldCount, desc->size, storageOffset, CScriptBase::TextOffset(), true, why)) return refuse(why);
                for (const CField& f : type->fields)
                    if (f.accessor && (!type->cb.text_get || !type->cb.text_set)) return refuse("'" + full + "' : the field '" + f.name + "' is an accessor string, the script needs text_get and text_set");

                {
                    std::lock_guard<std::mutex> lock(gMutex);
                    if (gTypes.count(full)) return SHARD_E_ALREADY_EXISTS;
                }
                std::shared_ptr<const CScriptType> shared = type;
                if (!ctx->Components().Register(full, Objects::Components::ComponentFactory([shared, make]() -> std::shared_ptr<Objects::Components::Component> {
                        return make(shared);
                    })))
                    return SHARD_E_ALREADY_EXISTS;
                {
                    std::lock_guard<std::mutex> lock(gMutex);
                    gTypes[full] = type;
                }
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // The object of the script `full` on the actor, if it is one of this plugin's
        ShardResult ObjectOf(ShardPluginCtx handle, const std::shared_ptr<Objects::Actor>& actor, const std::string& full, void** out)
        {
            *out = nullptr;
            for (const auto& component : actor->GetComponents()) {
                auto script = std::dynamic_pointer_cast<CScriptBase>(component);
                if (!script || script->Type().owner != PluginOwner(handle) || script->Type().name != full) continue;
                *out = script->Data();
                return SHARD_OK;
            }
            return SHARD_E_NOT_FOUND;
        }

        ShardResult SHARD_CALL GetObject(ShardPluginCtx handle, ShardActor actor, ShardStr name, void** out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = nullptr;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                auto a = ActorOf(actor);
                if (!a) return SHARD_E_STALE_HANDLE;
                return ObjectOf(handle, a, ctx->Id() + "." + ToString(name), out);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL AddToActor(ShardPluginCtx handle, ShardActor actor, ShardStr name, void** out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = nullptr;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                auto a = ActorOf(actor);
                if (!a) return SHARD_E_STALE_HANDLE;
                const std::string full = ctx->Id() + "." + ToString(name);
                {
                    std::lock_guard<std::mutex> lock(gMutex);
                    auto it = gTypes.find(full);
                    if (it == gTypes.end() || it->second->owner != PluginOwner(handle)) return SHARD_E_NOT_FOUND;
                }
                if (!a->AddComponentByName(full)) return SHARD_E_INTERNAL;
                return ObjectOf(handle, a, full, out);
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    void ReleaseScripts(PluginOwner owner)
    {
        std::lock_guard<std::mutex> lock(gMutex);
        for (auto it = gTypes.begin(); it != gTypes.end();) {
            if (it->second->owner == owner) {
                it->second->alive.store(false);
                it = gTypes.erase(it);
            } else {
                ++it;
            }
        }
    }

    const void* ScriptsApi()
    {
        static const ShardScriptsApi api = { sizeof(ShardScriptsApi), &RegisterScript, &GetObject, &AddToActor };
        return &api;
    }
}
