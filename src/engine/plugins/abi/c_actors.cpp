#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <shard/shard_actors.h>

#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/core/tags/tag.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/world.hpp"

// shard.actors : the actors of a world seen through handles. A handle is a slot of a table of weak pointers and a generation, so that an
// actor that was destroyed (or whose id was given to another one) fails with SHARD_E_STALE_HANDLE instead of designating something else.

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        struct Slot {
            std::weak_ptr<Objects::Actor> actor;
            const Objects::Actor* raw = nullptr;
            uint32_t generation = 1;
            bool used = false;
        };
        std::mutex gMutex;
        std::vector<Slot> gSlots;
        std::unordered_map<const Objects::Actor*, uint32_t> gByActor;
        std::vector<uint32_t> gFree;
        uint32_t gSinceSweep = 0;

        ShardActor Pack(const Slot& slot, uint32_t index) { return (ShardActor(slot.generation) << 32) | ShardActor(index + 1); }

        // gMutex held : slots whose actor is gone become free (their old handles stale)
        void Sweep()
        {
            for (uint32_t i = 0; i < gSlots.size(); ++i) {
                Slot& slot = gSlots[i];
                if (!slot.used || !slot.actor.expired()) continue;
                gByActor.erase(slot.raw);
                slot = Slot{ {}, nullptr, slot.generation + 1 == 0 ? 1 : slot.generation + 1, false };
                gFree.push_back(i);
            }
        }

        // The actor still stands in its world: Destroy takes its id out of the object manager
        bool Alive(const std::shared_ptr<Objects::Actor>& actor)
        {
            Core::IEngineContext* engine = actor->GetEngineContext();
            Core::ObjectIDManager* objects = engine ? engine->GetObjectIDManager() : nullptr;
            return objects && objects->GetObjectFromID(actor->GetID()).get() == actor.get();
        }

        ShardResult Resolve(ShardActor handle, std::shared_ptr<Objects::Actor>& out)
        {
            if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
            out = ActorOf(handle);
            return out ? SHARD_OK : SHARD_E_STALE_HANDLE;
        }

        Core::IEngineContext* Engine() { return &Core::GetEngine(); }

        ShardLocalTransform ToPose(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale)
        {
            ShardLocalTransform pose;
            pose.position[0] = position.x; pose.position[1] = position.y; pose.position[2] = position.z;
            pose.rotation[0] = rotation.x; pose.rotation[1] = rotation.y; pose.rotation[2] = rotation.z; pose.rotation[3] = rotation.w;
            pose.scale[0] = scale.x; pose.scale[1] = scale.y; pose.scale[2] = scale.z;
            return pose;
        }

        bool Finite(const ShardLocalTransform& p)
        {
            for (float v : p.position) if (!std::isfinite(v)) return false;
            for (float v : p.rotation) if (!std::isfinite(v)) return false;
            for (float v : p.scale) if (!std::isfinite(v)) return false;
            const float q = p.rotation[0] * p.rotation[0] + p.rotation[1] * p.rotation[1] + p.rotation[2] * p.rotation[2] + p.rotation[3] * p.rotation[3];
            return q > 1e-12f;
        }

        glm::quat QuatOf(const ShardLocalTransform& p) { return glm::normalize(glm::quat(p.rotation[3], p.rotation[0], p.rotation[1], p.rotation[2])); }

        // The first component of the actor whose serialized "type" is that
        std::shared_ptr<Objects::Components::Component> FindComponent(const std::shared_ptr<Objects::Actor>& actor, const std::string& type)
        {
            for (const auto& component : actor->GetComponents()) {
                const ordered_json saved = component->Serialize();
                if (saved.is_object() && saved.value("type", std::string()) == type) return component;
            }
            return nullptr;
        }

        bool ParseObject(ShardStr text, json& out)
        {
            const std::string s = ToString(text);
            if (s.find_first_not_of(" \t\r\n") == std::string::npos) { out = json::object(); return true; }
            out = json::parse(s, nullptr, false);
            return !out.is_discarded() && out.is_object();
        }

        // ---- The fields of a component, one at a time ----

        std::vector<std::string> SplitPath(const std::string& path)
        {
            std::vector<std::string> tokens;
            size_t from = path.empty() || path[0] != '/' ? 0 : 1;
            for (size_t at; (at = path.find('/', from)) != std::string::npos; from = at + 1) tokens.push_back(path.substr(from, at - from));
            tokens.push_back(path.substr(from));
            for (const std::string& token : tokens)
                if (token.empty()) return {};
            return tokens;
        }

        bool ParseIndex(const std::string& token, size_t& out)
        {
            if (token.empty() || token.size() > 6 || token.find_first_not_of("0123456789") != std::string::npos) return false;
            out = size_t(std::stoul(token));
            return true;
        }

        // What fills the gap when an array grows past its end : the zero of the kind of value that is written
        json Filler(const json& value)
        {
            if (value.is_number()) return 0;
            if (value.is_string()) return "";
            if (value.is_boolean()) return false;
            return nullptr;
        }

        // Sets (value) or removes (no value) the node that tokens[at..] designates in `node`
        ShardResult EditNode(json& node, const std::vector<std::string>& tokens, size_t at, const json* value)
        {
            const std::string& token = tokens[at];
            const bool last = at + 1 == tokens.size();
            if (node.is_object()) {
                if (last) {
                    if (!value) return node.erase(token) ? SHARD_OK : SHARD_E_NOT_FOUND;
                    node[token] = *value;
                    return SHARD_OK;
                }
                auto it = node.find(token);
                return it == node.end() ? SHARD_E_NOT_FOUND : EditNode(*it, tokens, at + 1, value);
            }
            if (node.is_array()) {
                if (token == "-") {
                    if (!last || !value) return SHARD_E_INVALID_ARG;
                    node.push_back(*value);
                    return SHARD_OK;
                }
                size_t index = 0;
                if (!ParseIndex(token, index)) return SHARD_E_INVALID_ARG;
                if (!last) return index < node.size() ? EditNode(node[index], tokens, at + 1, value) : SHARD_E_NOT_FOUND;
                if (!value) {
                    if (index >= node.size()) return SHARD_E_NOT_FOUND;
                    node.erase(node.begin() + std::ptrdiff_t(index));
                    return SHARD_OK;
                }
                if (index > 4096) return SHARD_E_INVALID_ARG;
                while (node.size() <= index) node.push_back(Filler(*value));
                node[index] = *value;
                return SHARD_OK;
            }
            return SHARD_E_NOT_FOUND;
        }

        const json* ReadNode(const json& node, const std::vector<std::string>& tokens, size_t at)
        {
            if (at == tokens.size()) return &node;
            const std::string& token = tokens[at];
            if (node.is_object()) {
                auto it = node.find(token);
                return it == node.end() ? nullptr : ReadNode(*it, tokens, at + 1);
            }
            size_t index = 0;
            if (node.is_array() && ParseIndex(token, index) && index < node.size()) return ReadNode(node[index], tokens, at + 1);
            return nullptr;
        }

        bool IsReflectedField(Objects::Components::Component& component, const std::string& name)
        {
            const ClassDescriptor* descriptor = component.GetDescriptor();
            if (!descriptor) return false;
            for (const FieldInfo* field : descriptor->fields)
                if (name == field->name && !(field->flags & ReadOnly)) return true;
            return false;
        }

        // Sets (value) or removes (no value) what `path` designates in the component
        ShardResult EditField(ShardActor actor, ShardStr type, ShardStr path, const json* value)
        {
            std::shared_ptr<Objects::Actor> a;
            const ShardResult r = Resolve(actor, a);
            if (r != SHARD_OK) return r;
            auto component = FindComponent(a, ToString(type));
            if (!component) return SHARD_E_NOT_FOUND;
            const std::vector<std::string> tokens = SplitPath(ToString(path));
            if (tokens.empty()) return SHARD_E_INVALID_ARG;
            const std::string& head = tokens[0];

            if (head == "active") {
                if (tokens.size() != 1 || !value || !value->is_boolean()) return SHARD_E_INVALID_ARG;
                if (value->get<bool>()) component->Activate();
                else component->DeActivate();
                return SHARD_OK;
            }

            json saved = json::parse(component->Serialize().dump());
            if (!saved.is_object() || !saved.contains(head)) return SHARD_E_NOT_FOUND;

            if (IsReflectedField(*component, head)) {
                // The field alone, through the component's change hook : what the inspector does when it edits it
                json field = saved[head];
                if (tokens.size() == 1) {
                    if (!value) return SHARD_E_INVALID_ARG;
                    field = *value;
                } else {
                    const ShardResult edited = EditNode(field, tokens, 1, value);
                    if (edited != SHARD_OK) return edited;
                }
                return component->ApplyField(head, field) ? SHARD_OK : SHARD_E_INVALID_ARG;
            }

            // What the reflection does not describe (the shapes of a body...) : the component reads it all again
            const ShardResult edited = EditNode(saved, tokens, 0, value);
            if (edited != SHARD_OK) return edited;
            component->Deserialize(saved);
            return SHARD_OK;
        }

        // ---- shard.actors ----

        int32_t SHARD_CALL IsValid(ShardActor actor)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                return Resolve(actor, a) == SHARD_OK ? SHARD_TRUE : SHARD_FALSE;
            } catch (...) { return SHARD_FALSE; }
        }

        ShardResult SHARD_CALL GetName(ShardActor actor, char* buffer, uint64_t capacity, uint64_t* size)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                const ShardResult r = Resolve(actor, a);
                return r != SHARD_OK ? r : CopyOut(a->GetName(), buffer, capacity, size);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetName(ShardActor actor, ShardStr name)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                a->SetName(ToString(name));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        int32_t SHARD_CALL IsActive(ShardActor actor)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                return Resolve(actor, a) == SHARD_OK && a->IsActive() ? SHARD_TRUE : SHARD_FALSE;
            } catch (...) { return SHARD_FALSE; }
        }

        ShardResult SHARD_CALL SetActive(ShardActor actor, int32_t active)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                if (active && !a->IsActive()) a->Activate();
                else if (!active && a->IsActive()) a->DeActivate();
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Destroy(ShardActor actor)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                a->Destroy();
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL GetTransform(ShardActor actor, ShardLocalTransform* out)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                if (!out) return SHARD_E_INVALID_ARG;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                if (!a->transform) return SHARD_E_NOT_FOUND;
                *out = ToPose(a->transform->position, a->transform->rotation, a->transform->scale);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetTransform(ShardActor actor, const ShardLocalTransform* pose)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                if (!pose || !Finite(*pose)) return SHARD_E_INVALID_ARG;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                if (!a->transform) return SHARD_E_NOT_FOUND;
                Objects::Components::Transform& t = *a->transform;
                const glm::vec3 position(pose->position[0], pose->position[1], pose->position[2]);
                const glm::vec3 scale(pose->scale[0], pose->scale[1], pose->scale[2]);
                const glm::quat rotation = QuatOf(*pose);
                if (t.position != position) t.SetPosition(position);
                if (t.rotation != rotation) t.SetRotation(rotation);
                if (t.scale != scale) t.SetScale(scale);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL GetWorldTransform(ShardActor actor, ShardLocalTransform* out)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                if (!out) return SHARD_E_INVALID_ARG;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                if (!a->transform) return SHARD_E_NOT_FOUND;
                *out = ToPose(a->transform->GetWorldPosition(), a->transform->GetWorldRotationQuat(), a->transform->GetWorldScale());
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetWorldTransform(ShardActor actor, const ShardLocalTransform* pose)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                if (!pose || !Finite(*pose)) return SHARD_E_INVALID_ARG;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                if (!a->transform) return SHARD_E_NOT_FOUND;
                const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(pose->position[0], pose->position[1], pose->position[2])) *
                                        glm::mat4_cast(QuatOf(*pose)) *
                                        glm::scale(glm::mat4(1.0f), glm::vec3(pose->scale[0], pose->scale[1], pose->scale[2]));
                return a->transform->SetFromWorldMatrix(world) ? SHARD_OK : SHARD_E_INVALID_ARG;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL GetParent(ShardActor actor, ShardActor* out)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                *out = ActorHandle(std::dynamic_pointer_cast<Objects::Actor>(a->GetParent()));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetParent(ShardActor actor, ShardActor parent, int32_t keepWorld)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                std::shared_ptr<Objects::Actor> p;
                if (parent) {
                    p = ActorOf(parent);
                    if (!p) return SHARD_E_STALE_HANDLE;
                }
                a->SetParent(p, keepWorld != 0);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ChildCount(ShardActor actor, uint32_t* count)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                if (!count) return SHARD_E_INVALID_ARG;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                *count = uint32_t(a->GetChildrenCount());
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ChildAt(ShardActor actor, uint32_t index, ShardActor* out)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                if (index >= uint32_t(a->GetChildrenCount())) return SHARD_E_NOT_FOUND;
                auto child = std::dynamic_pointer_cast<Objects::Actor>(a->GetChild(int(index)));
                if (!child) return SHARD_E_NOT_FOUND;
                *out = ActorHandle(child);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL AddTag(ShardActor actor, ShardStr tag)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                const Core::Tag t = Core::Tag::Request(ToString(tag));
                if (!t.IsValid()) return SHARD_E_NOT_FOUND;         // nobody declared it
                a->GetTags().Add(t);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL RemoveTag(ShardActor actor, ShardStr tag)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                const Core::Tag t = Core::Tag::Request(ToString(tag));
                if (!t.IsValid()) return SHARD_E_NOT_FOUND;
                return a->GetTags().Remove(t) ? SHARD_OK : SHARD_E_NOT_FOUND;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        int32_t SHARD_CALL HasTag(ShardActor actor, ShardStr tag)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                if (Resolve(actor, a) != SHARD_OK) return SHARD_FALSE;
                const Core::Tag t = Core::Tag::Request(ToString(tag));
                return t.IsValid() && a->GetTags().HasTag(t) ? SHARD_TRUE : SHARD_FALSE;
            } catch (...) { return SHARD_FALSE; }
        }

        // The world the anchor is in
        ShardResult WorldOf(ShardActor anchor, std::shared_ptr<Objects::Actor>& a, Worlds::World*& world)
        {
            const ShardResult r = Resolve(anchor, a);
            if (r != SHARD_OK) return r;
            world = a->world;
            return world ? SHARD_OK : SHARD_E_NOT_FOUND;
        }

        ShardResult SHARD_CALL Create(ShardActor anchor, ShardStr name, ShardActor parent, ShardActor* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                std::shared_ptr<Objects::Actor> a;
                Worlds::World* world = nullptr;
                const ShardResult r = WorldOf(anchor, a, world);
                if (r != SHARD_OK) return r;
                std::shared_ptr<Objects::Actor> p;
                if (parent) {
                    p = ActorOf(parent);
                    if (!p) return SHARD_E_STALE_HANDLE;
                }
                Core::IEngineContext* engine = Engine();
                auto created = Core::Object::CreateWithContext<Objects::Actor>(engine, ToString(name), engine);
                if (p) p->AddChild(created);
                else world->AddActor(created);
                *out = ActorHandle(created);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Spawn(ShardActor anchor, ShardStr actorJson, ShardActor parent, ShardActor* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                std::shared_ptr<Objects::Actor> a;
                Worlds::World* world = nullptr;
                const ShardResult r = WorldOf(anchor, a, world);
                if (r != SHARD_OK) return r;
                json data;
                if (!ParseObject(actorJson, data)) return SHARD_E_INVALID_ARG;
                std::shared_ptr<Objects::Actor> p;
                if (parent) {
                    p = ActorOf(parent);
                    if (!p) return SHARD_E_STALE_HANDLE;
                }
                auto spawned = Worlds::InstantiateActor(*world, data, p);
                if (!spawned) return SHARD_E_INVALID_ARG;
                *out = ActorHandle(spawned);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // Depth first, the roots and the children in the order of their ids
        std::shared_ptr<Objects::Actor> SearchByName(const std::shared_ptr<Objects::Actor>& actor, const std::string& name, Core::ObjectIDManager* objects)
        {
            if (actor->GetName() == name) return actor;
            std::vector<Core::ObjectID> ids = actor->GetChildrenID();
            std::sort(ids.begin(), ids.end());
            for (const Core::ObjectID& id : ids) {
                auto child = std::dynamic_pointer_cast<Objects::Actor>(objects->GetObjectFromID(id));
                if (!child) continue;
                if (auto found = SearchByName(child, name, objects)) return found;
            }
            return nullptr;
        }

        ShardResult SHARD_CALL FindByName(ShardActor anchor, ShardStr name, ShardActor* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                std::shared_ptr<Objects::Actor> a;
                Worlds::World* world = nullptr;
                const ShardResult r = WorldOf(anchor, a, world);
                if (r != SHARD_OK) return r;
                Core::ObjectIDManager* objects = Engine()->GetObjectIDManager();
                const auto rootMap = world->GetRootActors();
                std::vector<std::pair<Core::ObjectID, std::shared_ptr<Objects::Actor>>> roots(rootMap.begin(), rootMap.end());
                std::sort(roots.begin(), roots.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
                const std::string wanted = ToString(name);
                for (auto& [id, root] : roots) {
                    if (auto found = SearchByName(root, wanted, objects)) { *out = ActorHandle(found); return SHARD_OK; }
                }
                return SHARD_E_NOT_FOUND;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL FindByTag(ShardActor anchor, ShardStr tag, ShardActor* out, uint32_t capacity, uint32_t* count)
        {
            try {
                if (!count || (capacity && !out)) return SHARD_E_INVALID_ARG;
                *count = 0;
                std::shared_ptr<Objects::Actor> a;
                Worlds::World* world = nullptr;
                const ShardResult r = WorldOf(anchor, a, world);
                if (r != SHARD_OK) return r;
                const Core::Tag t = Core::Tag::Request(ToString(tag));
                if (!t.IsValid()) return SHARD_E_NOT_FOUND;
                const auto found = world->FindActorsWithTag(t);
                *count = uint32_t(found.size());
                for (uint32_t i = 0; i < capacity && i < found.size(); ++i) out[i] = ActorHandle(found[i]);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL AddComponent(ShardActor actor, ShardStr type, ShardStr fieldsJson)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                json fields;
                if (!ParseObject(fieldsJson, fields)) return SHARD_E_INVALID_ARG;
                auto component = a->AddComponentByName(ToString(type));
                if (!component) return SHARD_E_NOT_FOUND;
                if (!fields.empty()) component->Deserialize(fields);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        int32_t SHARD_CALL HasComponent(ShardActor actor, ShardStr type)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                if (Resolve(actor, a) != SHARD_OK) return SHARD_FALSE;
                return FindComponent(a, ToString(type)) ? SHARD_TRUE : SHARD_FALSE;
            } catch (...) { return SHARD_FALSE; }
        }

        ShardResult SHARD_CALL RemoveComponent(ShardActor actor, ShardStr type)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                auto component = FindComponent(a, ToString(type));
                if (!component) return SHARD_E_NOT_FOUND;
                if (std::dynamic_pointer_cast<Objects::Components::Transform>(component)) return SHARD_E_INVALID_ARG;
                a->RemoveComponent(component);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL GetComponent(ShardActor actor, ShardStr type, char* buffer, uint64_t capacity, uint64_t* size)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                auto component = FindComponent(a, ToString(type));
                if (!component) return SHARD_E_NOT_FOUND;
                return CopyOut(component->Serialize().dump(), buffer, capacity, size);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetComponent(ShardActor actor, ShardStr type, ShardStr fieldsJson)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                json fields;
                if (!ParseObject(fieldsJson, fields)) return SHARD_E_INVALID_ARG;
                auto component = FindComponent(a, ToString(type));
                if (!component) return SHARD_E_NOT_FOUND;
                // The fields given over what the component holds: the others keep their value
                json merged = json::parse(component->Serialize().dump());
                for (auto it = fields.begin(); it != fields.end(); ++it) merged[it.key()] = it.value();
                component->Deserialize(merged);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL GetComponentField(ShardActor actor, ShardStr type, ShardStr path, char* buffer, uint64_t capacity, uint64_t* size)
        {
            try {
                std::shared_ptr<Objects::Actor> a;
                const ShardResult r = Resolve(actor, a);
                if (r != SHARD_OK) return r;
                auto component = FindComponent(a, ToString(type));
                if (!component) return SHARD_E_NOT_FOUND;
                const std::vector<std::string> tokens = SplitPath(ToString(path));
                if (tokens.empty()) return SHARD_E_INVALID_ARG;
                if (tokens.size() == 1 && tokens[0] == "active") return CopyOut(component->Active() ? "true" : "false", buffer, capacity, size);
                const json saved = json::parse(component->Serialize().dump());
                const json* node = ReadNode(saved, tokens, 0);
                return node ? CopyOut(node->dump(), buffer, capacity, size) : SHARD_E_NOT_FOUND;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetComponentField(ShardActor actor, ShardStr type, ShardStr path, ShardStr valueJson)
        {
            try {
                const json value = json::parse(ToString(valueJson), nullptr, false);
                if (value.is_discarded()) return SHARD_E_INVALID_ARG;
                return EditField(actor, type, path, &value);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL EraseComponentField(ShardActor actor, ShardStr type, ShardStr path)
        {
            try {
                return EditField(actor, type, path, nullptr);
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    ShardActor ActorHandle(const std::shared_ptr<Objects::Actor>& actor)
    {
        if (!actor) return 0;
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gByActor.find(actor.get());
        if (it != gByActor.end()) {
            Slot& slot = gSlots[it->second];
            if (slot.used && slot.actor.lock() == actor) return Pack(slot, it->second);
            gByActor.erase(it);             // the address belongs to another actor now
        }
        if (++gSinceSweep >= 256) { gSinceSweep = 0; Sweep(); }
        uint32_t index;
        if (!gFree.empty()) { index = gFree.back(); gFree.pop_back(); }
        else { index = uint32_t(gSlots.size()); gSlots.emplace_back(); }
        Slot& slot = gSlots[index];
        slot.actor = actor;
        slot.raw = actor.get();
        slot.used = true;
        gByActor[actor.get()] = index;
        return Pack(slot, index);
    }

    std::shared_ptr<Objects::Actor> ActorOf(ShardActor handle)
    {
        std::shared_ptr<Objects::Actor> actor;
        {
            std::lock_guard<std::mutex> lock(gMutex);
            const uint64_t index = handle & 0xFFFFFFFFull;
            if (index == 0 || index > gSlots.size()) return nullptr;
            const Slot& slot = gSlots[size_t(index - 1)];
            if (!slot.used || slot.generation != uint32_t(handle >> 32)) return nullptr;
            actor = slot.actor.lock();
        }
        return actor && Alive(actor) ? actor : nullptr;
    }

    std::string ActorPath(ShardActor handle)
    {
        std::shared_ptr<Objects::Actor> actor = ActorOf(handle);
        if (!actor) return {};
        std::vector<std::string> names;
        for (std::shared_ptr<Objects::WorldObject> at = actor; at; at = at->GetParent()) {
            auto a = std::dynamic_pointer_cast<Objects::Actor>(at);
            if (!a) break;
            names.push_back(a->GetName());
        }
        std::string path;
        for (auto it = names.rbegin(); it != names.rend(); ++it) path += (path.empty() ? "" : "/") + *it;
        return path;
    }

    ShardActor ActorAtPath(const std::shared_ptr<Objects::Actor>& anchor, const std::string& path)
    {
        if (!anchor || !anchor->world || path.empty()) return 0;
        Core::ObjectIDManager* objects = anchor->GetEngineContext() ? anchor->GetEngineContext()->GetObjectIDManager() : nullptr;
        if (!objects) return 0;
        std::vector<std::string> parts;
        size_t from = 0;
        for (size_t at; (at = path.find('/', from)) != std::string::npos; from = at + 1) parts.push_back(path.substr(from, at - from));
        parts.push_back(path.substr(from));

        // The roots, then the children, each level in the order of the ids
        const auto rootMap = anchor->world->GetRootActors();
        std::vector<std::pair<Core::ObjectID, std::shared_ptr<Objects::Actor>>> level(rootMap.begin(), rootMap.end());
        std::sort(level.begin(), level.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        std::shared_ptr<Objects::Actor> found;
        for (size_t depth = 0; depth < parts.size(); ++depth) {
            found = nullptr;
            for (auto& [id, candidate] : level)
                if (candidate->GetName() == parts[depth]) { found = candidate; break; }
            if (!found) return 0;
            if (depth + 1 == parts.size()) break;
            std::vector<Core::ObjectID> ids = found->GetChildrenID();
            std::sort(ids.begin(), ids.end());
            level.clear();
            for (const Core::ObjectID& id : ids)
                if (auto child = std::dynamic_pointer_cast<Objects::Actor>(objects->GetObjectFromID(id))) level.emplace_back(id, child);
        }
        return ActorHandle(found);
    }

    const void* ActorsApi()
    {
        static const ShardActorsApi api = {
            sizeof(ShardActorsApi),
            &IsValid, &GetName, &SetName, &IsActive, &SetActive, &Destroy,
            &GetTransform, &SetTransform, &GetWorldTransform, &SetWorldTransform,
            &GetParent, &SetParent, &ChildCount, &ChildAt,
            &AddTag, &RemoveTag, &HasTag,
            &Create, &Spawn, &FindByName, &FindByTag,
            &AddComponent, &HasComponent, &RemoveComponent, &GetComponent, &SetComponent,
            &GetComponentField, &SetComponentField, &EraseComponentField };
        return &api;
    }
}
