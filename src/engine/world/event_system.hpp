#pragma once

#include "engine/physics/physics_manager.hpp"

#include "engine/world/objectID.hpp"

#include <unordered_map>
#include <functional>
#include <typeindex>
#include <vector>

namespace Shard::Engine::Objects::Components{
    class PhysicsBody;
}

namespace Shard::Engine::Events {

    struct Event {
        virtual ~Event() = default;
        Core::ObjectID sourceObjectID;
        explicit Event(Core::ObjectID source) : sourceObjectID(source) {}
    };

    // Common Event Definitions
    struct KeyPressedEvent : public Event {
        int keyCode;
        bool repeated;
        KeyPressedEvent(const int key, const bool rep, Core::ObjectID source) : keyCode(key), repeated(rep), Event(source) {}
    };

    struct ContactAddedEvent : public Event {
        const Objects::Components::PhysicsBody& otherBody;
        const ContactManifold &contactManifold;
        ContactSettings &contactSettings;
        ContactAddedEvent(const Objects::Components::PhysicsBody& b2, const ContactManifold &manifold, ContactSettings &settings, Core::ObjectID source)
             : otherBody(b2), contactManifold(manifold), contactSettings(settings), Event(source) {}
    };

    struct ContactPersistedEvent : public Event {
        const Objects::Components::PhysicsBody& otherBody;
        const ContactManifold &contactManifold;
        ContactSettings &contactSettings;
        ContactPersistedEvent(const Objects::Components::PhysicsBody& b2, const ContactManifold &manifold, ContactSettings &settings, Core::ObjectID source)
             : otherBody(b2), contactManifold(manifold), contactSettings(settings), Event(source) {}
    };

    struct ContactRemovedEvent : public Event {
        const Objects::Components::PhysicsBody& otherBody;
        ContactRemovedEvent(
        const Objects::Components::PhysicsBody& b2, Core::ObjectID source) : otherBody(b2), Event(source) {}
    };

    enum LevelChangeType{
        DESTROYED,
        CREATED,
        MOVED,
        ACTIVATED,
        DEACTIVATED,
        LOADED,
        UNLOADED
    };

    struct LevelStructureChangedEvent : public Event{
        const int levelAssetID;
        const LevelChangeType changeType;
        const std::string actorName;
        LevelStructureChangedEvent(
        const int& levelAssetID, const LevelChangeType changeType, const std::string actorName, Core::ObjectID source) : levelAssetID(levelAssetID), actorName(actorName), changeType(changeType), Event(source) {}
    };

    // EventDispatcher
    class EventDispatcher {
        public:

            using ComponentID = uint32_t;

            // Subscriptions
            template<typename T>
            void subscribeGlobal(std::function<void(const T&)> callback) {
                globalSubscribers[typeid(T)].emplace_back([cb = std::move(callback)](const Event& e) {
                    cb(static_cast<const T&>(e));
                });
            }

            template<typename T>
            void subscribeToLevel(int levelID, std::function<void(const T&)> callback) {
                levelSubscribers[levelID][typeid(T)].emplace_back([cb = std::move(callback)](const Event& e) {
                    cb(static_cast<const T&>(e));
                });
            }

            template<typename T>
            void subscribeToActor(Core::ObjectID actorID, std::function<void(const T&)> callback) {
                actorSubscribers[actorID][typeid(T)].emplace_back([cb = std::move(callback)](const Event& e) {
                    cb(static_cast<const T&>(e));
                });
            }

            template<typename T>
            void subscribeToComponent(ComponentID componentID, std::function<void(const T&)> callback) {
                componentSubscribers[componentID][typeid(T)].emplace_back(
                    [cb = std::move(callback)](const Event& e) {
                        cb(static_cast<const T&>(e));
                    }
                );
            }

            // Emission
            template<typename T>
            void emitGlobal(const T& event) {
                dispatchTo(globalSubscribers, event);
            }

            template<typename T>
            void emitToLevel(int levelID, const T& event) {
                auto it = levelSubscribers.find(levelID);
                if (it != levelSubscribers.end()) {
                    dispatchTo(it->second, event);
                }
            }

            template<typename T>
            void emitToActor(Core::ObjectID actorID, const T& event) {
                auto it = actorSubscribers.find(actorID);
                if (it != actorSubscribers.end()) {
                    dispatchTo(it->second, event);
                }
            }

            template<typename T>
            void emitToComponent(ComponentID componentID, const T& event) {
                auto it = componentSubscribers.find(componentID);
                if (it != componentSubscribers.end()) {
                    dispatchTo(it->second, event);
                }
            }

                
        private:

            using Handler = std::function<void(const Event&)>;
            using SubscriberMap = std::unordered_map<std::type_index, std::vector<Handler>>;

            template<typename T>
            void dispatchTo(SubscriberMap& map, const T& event) {
                auto it = map.find(typeid(T));
                if (it != map.end()) {
                    for (auto& handler : it->second) {
                        handler(event);
                    }
                }
            }

            // Subscriber storage
            SubscriberMap globalSubscribers;
            std::unordered_map<int, SubscriberMap> levelSubscribers;
            std::unordered_map<Core::ObjectID, SubscriberMap> actorSubscribers;
            std::unordered_map<ComponentID, SubscriberMap> componentSubscribers;
    };
}