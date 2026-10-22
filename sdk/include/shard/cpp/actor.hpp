// Shard C++ SDK : actors, the things a world holds (shard_actors.h).
//
//   Shard::Actor enemy = scene.Spawn(R"({ "name": "Enemy", "components": [] })");
//   enemy.SetPosition({ 0.f, 1.f, -5.f });
//   enemy.AddTag("Enemy");
//   if (Health* health = enemy.GetScript<Health>()) health->Damage(10.f);
//
// An Actor is a handle: copying it is free, and it stays safe when the actor is destroyed (IsValid() turns false, the getters return zeroes, the
// setters do nothing). Use it from the callbacks of a script (Shard::Script) : not from a system, which runs beside others.
#pragma once

#include <cstring>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include <shard/shard_actors.h>
#include <shard/shard_audio.h>
#include <shard/shard_physics.h>
#include <shard/shard_scripts.h>
#include <shard/shard_worlds.h>
#include <shard/cpp/detail.hpp>
#include <shard/cpp/math.hpp>

namespace Shard {

    class Actor;

    namespace detail {
        /// The name each script class was registered under (filled by Scripts::Register in OnLoad), so that any file can ask for a class's script
        inline std::unordered_map<std::type_index, std::string>& ScriptNames()
        {
            static std::unordered_map<std::type_index, std::string> names;
            return names;
        }
        inline const std::string* ScriptNameOf(const std::type_info& type)
        {
            auto it = ScriptNames().find(std::type_index(type));
            return it == ScriptNames().end() ? nullptr : &it->second;
        }

        inline const ShardActorsApi& Actors() { return Require<ShardActorsApi>(SHARD_IFACE_ACTORS); }
        inline const ShardScriptsApi& ScriptsApi() { return Require<ShardScriptsApi>(SHARD_IFACE_SCRIPTS); }
        inline const ShardPhysicsApi& PhysicsApi() { return Require<ShardPhysicsApi>(SHARD_IFACE_PHYSICS); }
        inline const ShardAudioApi& AudioApi() { return Require<ShardAudioApi>(SHARD_IFACE_AUDIO); }
        inline const ShardWorldsApi& WorldsApi() { return Require<ShardWorldsApi>(SHARD_IFACE_WORLDS); }

        inline void ToArray(Vec3 v, float out[3]) { out[0] = v.x; out[1] = v.y; out[2] = v.z; }
        inline Vec3 FromArray(const float v[3]) { return Vec3{ v[0], v[1], v[2] }; }

        inline Pose ToPose(const ShardLocalTransform& t)
        {
            return Pose{ { t.position[0], t.position[1], t.position[2] }, { t.rotation[0], t.rotation[1], t.rotation[2], t.rotation[3] },
                         { t.scale[0], t.scale[1], t.scale[2] } };
        }
        inline ShardLocalTransform ToTransform(const Pose& p)
        {
            ShardLocalTransform t;
            t.position[0] = p.position.x; t.position[1] = p.position.y; t.position[2] = p.position.z;
            t.rotation[0] = p.rotation.x; t.rotation[1] = p.rotation.y; t.rotation[2] = p.rotation.z; t.rotation[3] = p.rotation.w;
            t.scale[0] = p.scale.x; t.scale[1] = p.scale.y; t.scale[2] = p.scale.z;
            return t;
        }
    }

    /// The world an actor is in: where to create, spawn and find other actors
    class Scene {
    public:
        Scene() = default;
        explicit Scene(ShardActor anchor) : m_Anchor(anchor) {}

        /// A new actor (with its Transform) at the root of the scene, or under `parent`. Throws Shard::Error if the scene is gone.
        Actor Create(std::string_view name, Actor parent);
        Actor Create(std::string_view name);
        /// An actor built from the JSON of a world file's actor (name, components, children) : what the editor saves a prefab as
        Actor Spawn(std::string_view actorJson, Actor parent);
        Actor Spawn(std::string_view actorJson);
        /// The first actor of that name (depth first, in the order of the ids), or an invalid one
        Actor Find(std::string_view name) const;
        /// Builds a prefab, an actor stored as JSON in the content of a module : a "mod://<id>/prefabs/crate.json" address, a logical path
        /// ("prefabs/crate.json") or a bare name ("crate"). Throws Shard::Error if there is no such prefab.
        Actor SpawnPrefab(std::string_view prefab, Actor parent);
        Actor SpawnPrefab(std::string_view prefab);
        /// The actors that have the tag or one under it ("Enemy" finds an "Enemy.Undead"), in the order of their ids
        std::vector<Actor> FindWithTag(std::string_view tag) const;

    private:
        ShardActor m_Anchor = 0;
    };

    class Actor {
    public:
        Actor() = default;
        explicit Actor(ShardActor handle) : m_Handle(handle) {}

        ShardActor Handle() const { return m_Handle; }
        bool IsValid() const { return m_Handle != 0 && Api().is_valid(m_Handle) == SHARD_TRUE; }
        explicit operator bool() const { return IsValid(); }
        friend bool operator==(const Actor& a, const Actor& b) { return a.m_Handle == b.m_Handle; }
        friend bool operator!=(const Actor& a, const Actor& b) { return a.m_Handle != b.m_Handle; }

        // ---- Identity and state ----
        std::string Name() const { return Read([&](char* buffer, uint64_t capacity, uint64_t* size) { return Api().get_name(m_Handle, buffer, capacity, size); }); }
        void SetName(std::string_view name) const { Api().set_name(m_Handle, Str(name)); }
        bool IsActive() const { return Api().is_active(m_Handle) == SHARD_TRUE; }
        void SetActive(bool active) const { Api().set_active(m_Handle, active ? 1 : 0); }
        /// Removes the actor, its components and its children. Its scripts get OnDestroyed.
        void Destroy() const { Api().destroy(m_Handle); }

        // ---- Pose : relative to the parent (Local) or in the world ----
        Pose LocalPose() const { ShardLocalTransform t{}; return Api().get_transform(m_Handle, &t) == SHARD_OK ? detail::ToPose(t) : Pose{}; }
        void SetLocalPose(const Pose& pose) const { const ShardLocalTransform t = detail::ToTransform(pose); Api().set_transform(m_Handle, &t); }
        Pose WorldPose() const { ShardLocalTransform t{}; return Api().get_world_transform(m_Handle, &t) == SHARD_OK ? detail::ToPose(t) : Pose{}; }
        void SetWorldPose(const Pose& pose) const { const ShardLocalTransform t = detail::ToTransform(pose); Api().set_world_transform(m_Handle, &t); }

        Vec3 Position() const { return LocalPose().position; }
        void SetPosition(Vec3 position) const { Pose p = LocalPose(); p.position = position; SetLocalPose(p); }
        Quat Rotation() const { return LocalPose().rotation; }
        void SetRotation(Quat rotation) const { Pose p = LocalPose(); p.rotation = rotation; SetLocalPose(p); }
        Vec3 Scale() const { return LocalPose().scale; }
        void SetScale(Vec3 scale) const { Pose p = LocalPose(); p.scale = scale; SetLocalPose(p); }
        Vec3 WorldPosition() const { return WorldPose().position; }
        void SetWorldPosition(Vec3 position) const { Pose p = WorldPose(); p.position = position; SetWorldPose(p); }
        Quat WorldRotation() const { return WorldPose().rotation; }
        void SetWorldRotation(Quat rotation) const { Pose p = WorldPose(); p.rotation = rotation; SetWorldPose(p); }

        /// Moves the actor by `delta`, in its parent's space
        void Translate(Vec3 delta) const { SetPosition(Position() + delta); }
        /// Moves the actor by `delta`, in the world's axes
        void TranslateWorld(Vec3 delta) const { SetWorldPosition(WorldPosition() + delta); }
        /// The directions the actor faces, in the world (it looks down its -Z)
        Vec3 Forward() const { return WorldPose().Forward(); }
        Vec3 Right() const { return WorldPose().Right(); }
        Vec3 Up() const { return WorldPose().Up(); }
        /// Turns the actor to face a point of the world
        void LookAt(Vec3 target, Vec3 up = { 0.f, 1.f, 0.f }) const
        {
            Pose p = WorldPose();
            p.rotation = LookRotation(target - p.position, up);
            SetWorldPose(p);
        }

        // ---- Hierarchy ----
        Actor Parent() const { ShardActor p = 0; Api().get_parent(m_Handle, &p); return Actor(p); }
        /// parent invalid: the root of the world. keepWorld: the actor stays where it is.
        void SetParent(Actor parent, bool keepWorld = true) const { Api().set_parent(m_Handle, parent.m_Handle, keepWorld ? 1 : 0); }
        std::vector<Actor> Children() const
        {
            std::vector<Actor> out;
            uint32_t count = 0;
            if (Api().child_count(m_Handle, &count) != SHARD_OK) return out;
            for (uint32_t i = 0; i < count; ++i) {
                ShardActor child = 0;
                if (Api().child_at(m_Handle, i, &child) == SHARD_OK) out.push_back(Actor(child));
            }
            return out;
        }

        // ---- Tags (a name declared in the project or by a plugin's manifest) ----
        /// False if the tag is not declared
        bool AddTag(std::string_view tag) const { return Api().add_tag(m_Handle, Str(tag)) == SHARD_OK; }
        bool RemoveTag(std::string_view tag) const { return Api().remove_tag(m_Handle, Str(tag)) == SHARD_OK; }
        /// True for the tag or one under it
        bool HasTag(std::string_view tag) const { return Api().has_tag(m_Handle, Str(tag)) == SHARD_TRUE; }

        // ---- The engine's components as typed handles (shard/cpp/engine_components.hpp) : Model, PhysicsBody, Light, Camera, AudioSource... ----
        /// Adds a component and returns its handle, to set its fields : `actor.Add<Shard::Light>().SetIntensity(3.f)`. Throws Shard::Error.
        template <typename T>
        T Add() const
        {
            AddComponent(T::kType);
            return T(*this);
        }
        /// The handle of the component the actor has, which tells whether there is one : `if (auto body = actor.Get<Shard::PhysicsBody>())`
        template <typename T>
        T Get() const { return T(*this); }
        template <typename T>
        bool Has() const { return HasComponent(T::kType); }
        template <typename T>
        bool Remove() const { return RemoveComponent(T::kType); }

        // ---- The same components by the name the world file calls them ("light", "model"...), their fields as JSON : for a type that has no
        // class in engine_components.hpp, or to build a component from a file ----
        /// Throws Shard::Error if the type is unknown. `fieldsJson` : the fields to set, a JSON object, or empty.
        void AddComponent(std::string_view type, std::string_view fieldsJson = {}) const { detail::Check(Api().add_component(m_Handle, Str(type), Str(fieldsJson)), "add_component '" + std::string(type) + "'"); }
        bool HasComponent(std::string_view type) const { return Api().has_component(m_Handle, Str(type)) == SHARD_TRUE; }
        bool RemoveComponent(std::string_view type) const { return Api().remove_component(m_Handle, Str(type)) == SHARD_OK; }
        /// The component as the world file writes it, empty if the actor has none
        std::string ComponentJson(std::string_view type) const { return Read([&](char* b, uint64_t c, uint64_t* s) { return Api().get_component(m_Handle, Str(type), b, c, s); }); }
        /// Sets the fields the JSON object holds; the others keep their value. Throws Shard::Error if the actor has no such component.
        void SetComponentJson(std::string_view type, std::string_view fieldsJson) const { detail::Check(Api().set_component(m_Handle, Str(type), Str(fieldsJson)), "set_component '" + std::string(type) + "'"); }

        // ---- Physics : the actor needs a "physics_body" component (AddComponent("physics_body")). False : it has none, or no live body yet ----
        Vec3 Velocity() const { float v[3] = {}; detail::PhysicsApi().get_velocity(m_Handle, v); return detail::FromArray(v); }
        bool SetVelocity(Vec3 velocity) const { float v[3]; detail::ToArray(velocity, v); return detail::PhysicsApi().set_velocity(m_Handle, v) == SHARD_OK; }
        Vec3 AngularVelocity() const { float v[3] = {}; detail::PhysicsApi().get_angular_velocity(m_Handle, v); return detail::FromArray(v); }
        bool SetAngularVelocity(Vec3 velocity) const { float v[3]; detail::ToArray(velocity, v); return detail::PhysicsApi().set_angular_velocity(m_Handle, v) == SHARD_OK; }
        /// A force acts for the step it is added in : call it every step to push for longer. An impulse changes the velocity at once.
        bool AddForce(Vec3 force) const { float f[3]; detail::ToArray(force, f); return detail::PhysicsApi().add_force(m_Handle, f) == SHARD_OK; }
        bool AddForceAtPoint(Vec3 force, Vec3 worldPoint) const
        {
            float f[3], p[3];
            detail::ToArray(force, f);
            detail::ToArray(worldPoint, p);
            return detail::PhysicsApi().add_force_at_point(m_Handle, f, p) == SHARD_OK;
        }
        bool AddTorque(Vec3 torque) const { float t[3]; detail::ToArray(torque, t); return detail::PhysicsApi().add_torque(m_Handle, t) == SHARD_OK; }
        bool AddImpulse(Vec3 impulse) const { float i[3]; detail::ToArray(impulse, i); return detail::PhysicsApi().add_impulse(m_Handle, i) == SHARD_OK; }
        bool AddImpulseAtPoint(Vec3 impulse, Vec3 worldPoint) const
        {
            float i[3], p[3];
            detail::ToArray(impulse, i);
            detail::ToArray(worldPoint, p);
            return detail::PhysicsApi().add_impulse_at_point(m_Handle, i, p) == SHARD_OK;
        }
        bool AddAngularImpulse(Vec3 impulse) const { float i[3]; detail::ToArray(impulse, i); return detail::PhysicsApi().add_angular_impulse(m_Handle, i) == SHARD_OK; }
        /// What the body falls with now : the world's gravity and the gravity fields where it is, or its own
        Vec3 Gravity() const { float g[3] = {}; detail::PhysicsApi().get_gravity(m_Handle, g); return detail::FromArray(g); }
        /// Gives the body a gravity of its own : the world's gravity and the fields no longer reach it
        bool SetCustomGravity(Vec3 gravity) const { float g[3]; detail::ToArray(gravity, g); return detail::PhysicsApi().set_custom_gravity(m_Handle, g) == SHARD_OK; }

        // ---- Audio : the actor needs an "audio" component (AddComponent("audio", ...)) ----
        bool PlayAudio() const { return detail::AudioApi().play(m_Handle) == SHARD_OK; }
        bool PauseAudio() const { return detail::AudioApi().pause(m_Handle) == SHARD_OK; }
        bool SetAudioVolume(float volume) const { return detail::AudioApi().set_volume(m_Handle, volume) == SHARD_OK; }

        // ---- Scripts of this plugin ----
        /// The script of that class on the actor, null if it has none (a class registered with SHARD_REGISTER_SCRIPT)
        template <typename T>
        T* GetScript() const
        {
            const std::string* name = detail::ScriptNameOf(typeid(T));
            void* object = nullptr;
            if (!name || detail::ScriptsApi().get_object(detail::PluginHandle(), m_Handle, Str(*name), &object) != SHARD_OK) return nullptr;
            return static_cast<T*>(object);
        }
        /// Adds the script to the actor and returns it. Throws Shard::Error if it cannot.
        template <typename T>
        T* AddScript() const
        {
            const std::string* name = detail::ScriptNameOf(typeid(T));
            if (!name) throw Error(SHARD_E_NOT_FOUND, std::string("add script : ") + typeid(T).name() + " is not a registered script (SHARD_REGISTER_SCRIPT)");
            void* object = nullptr;
            detail::Check(detail::ScriptsApi().add_to_actor(detail::PluginHandle(), m_Handle, Str(*name), &object), "add script '" + *name + "'");
            return static_cast<T*>(object);
        }

        /// The world this actor is in
        Scene GetScene() const { return Scene(m_Handle); }

    private:
        static const ShardActorsApi& Api() { return detail::Actors(); }

        // A "text out" function of the ABI: asks for the length, then for the text
        template <typename Fn>
        static std::string Read(Fn&& call)
        {
            std::string out;
            uint64_t size = 0;
            if (call(nullptr, uint64_t(0), &size) != SHARD_OK || size == 0) return out;
            out.assign(size_t(size), '\0');
            if (call(&out[0], size, &size) != SHARD_OK) out.clear();
            return out;
        }

        ShardActor m_Handle = 0;
    };

    inline Actor Scene::Create(std::string_view name, Actor parent)
    {
        ShardActor out = 0;
        detail::Check(detail::Actors().create(m_Anchor, Str(name), parent.Handle(), &out), "create actor '" + std::string(name) + "'");
        return Actor(out);
    }
    inline Actor Scene::Create(std::string_view name) { return Create(name, Actor()); }
    inline Actor Scene::SpawnPrefab(std::string_view prefab, Actor parent)
    {
        ShardActor out = 0;
        detail::Check(detail::WorldsApi().spawn_prefab(m_Anchor, Str(prefab), parent.Handle(), &out), "spawn prefab '" + std::string(prefab) + "'");
        return Actor(out);
    }
    inline Actor Scene::SpawnPrefab(std::string_view prefab) { return SpawnPrefab(prefab, Actor()); }
    inline Actor Scene::Spawn(std::string_view actorJson, Actor parent)
    {
        ShardActor out = 0;
        detail::Check(detail::Actors().spawn(m_Anchor, Str(actorJson), parent.Handle(), &out), "spawn actor");
        return Actor(out);
    }
    inline Actor Scene::Spawn(std::string_view actorJson) { return Spawn(actorJson, Actor()); }
    inline Actor Scene::Find(std::string_view name) const
    {
        ShardActor out = 0;
        return detail::Actors().find_by_name(m_Anchor, Str(name), &out) == SHARD_OK ? Actor(out) : Actor();
    }
    inline std::vector<Actor> Scene::FindWithTag(std::string_view tag) const
    {
        std::vector<Actor> found;
        uint32_t count = 0;
        if (detail::Actors().find_by_tag(m_Anchor, Str(tag), nullptr, 0, &count) != SHARD_OK || count == 0) return found;
        std::vector<ShardActor> handles(count);
        if (detail::Actors().find_by_tag(m_Anchor, Str(tag), handles.data(), count, &count) != SHARD_OK) return found;
        for (uint32_t i = 0; i < count && i < handles.size(); ++i) found.push_back(Actor(handles[i]));
        return found;
    }

    /// An Actor member of a script is a reference to another actor : the host saves it by the actor's path in the world (SHARD_FIELD_ACTOR) and
    /// finds it again when the world is loaded, or when a copy of the world is made for play mode. IsValid() says whether it points at something.
    static_assert(sizeof(Actor) == sizeof(ShardActor), "an Actor is its handle");
    template <> struct FieldType<Actor> { static constexpr uint32_t value = SHARD_FIELD_ACTOR; };
}
