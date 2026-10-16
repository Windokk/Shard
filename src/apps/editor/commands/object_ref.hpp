#pragma once

#include "engine/world/engine.hpp"
#include "engine/world/object.hpp"
#include "engine/world/objectID.hpp"

#include <memory>
#include <unordered_map>

namespace Shard::Editor::Commands{

    /// @brief A handle on an object (actor, component) that the undo history keeps instead of its ObjectID.
    /// An id is released when its object is destroyed and handed to the next object created, so a command
    /// holding one could end up editing something else. A ref resolves to nothing once its object is gone,
    /// and the commands that destroy and rebuild objects (see ActorSnapshotCommand) point the refs of the old
    /// ones to the new ones : the other commands of the history keep working on the rebuilt objects.
    struct ObjectRef {
        Engine::Core::ObjectID id = Engine::Core::ObjectID(-1);
        std::weak_ptr<Engine::Core::Object> object;

        std::shared_ptr<Engine::Core::Object> Resolve() const {
            std::shared_ptr<Engine::Core::Object> live = object.lock();
            if (!live)
                return nullptr;

            // A destroyed object leaves the id manager, even while something still holds it
            if (Engine::Core::GetEngine().GetObjectIDManager()->GetObjectFromID(id) != live)
                return nullptr;

            return live;
        }

        template<typename T>
        std::shared_ptr<T> Resolve() const {
            return std::dynamic_pointer_cast<T>(Resolve());
        }
    };

    class ObjectRefs {
        public:
            /// The ref of a live object : the same one for everyone asking, so a rebind reaches all of them
            static std::shared_ptr<ObjectRef> Get(const std::shared_ptr<Engine::Core::Object>& object) {
                if (!object)
                    return nullptr;

                auto& refs = Registry();
                const int key = object->GetID().GetAsInt();

                auto it = refs.find(key);
                if (it != refs.end())
                    if (std::shared_ptr<ObjectRef> ref = it->second.lock(); ref && ref->Resolve() == object)
                        return ref;

                auto ref = std::make_shared<ObjectRef>();
                ref->id = object->GetID();
                ref->object = object;
                refs[key] = ref;
                return ref;
            }

            /// Points a ref to the object that replaces the one it was on
            static void Rebind(const std::shared_ptr<ObjectRef>& ref, const std::shared_ptr<Engine::Core::Object>& object) {
                if (!ref || !object)
                    return;

                ref->id = object->GetID();
                ref->object = object;
                Registry()[object->GetID().GetAsInt()] = ref;
            }

            static void Clear() {
                Registry().clear();
            }

        private:
            static std::unordered_map<int, std::weak_ptr<ObjectRef>>& Registry() {
                static std::unordered_map<int, std::weak_ptr<ObjectRef>> refs;
                return refs;
            }
    };
}
