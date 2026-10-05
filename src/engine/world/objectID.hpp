#pragma once

#include <string>
#include <atomic>
#include <unordered_set>
#include <map>
#include <cstddef>
#include <functional>
#include <memory>

#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Core
{
    class Object;
    
    class ObjectID {
        public:
            
            ObjectID() : packed(0) {}
            explicit ObjectID(int value) : packed(value) {}

            
            int GetAsInt() const {
                return packed;
            }

            std::string GetAsString() const {
                return std::to_string(GetAsInt());
            }

            
            bool operator==(const ObjectID& other) const { return packed == other.packed; }
            bool operator!=(const ObjectID& other) const { return !(*this == other); }
            bool operator<(const ObjectID& other) const { return packed < other.packed; }
    
        private:
            int packed;
    };

    class ObjectIDManager {
        public:

            // WARNING: Invalidates all existing ObjectIDs.
            // Must not be used while IDs are still referenced externally.
            void Reset() {
                ObjectIDMap.clear();
                availableIDs.clear();
                nextId = 1;
            }

            void DestroyID(const ObjectID& id) {
                availableIDs.insert(id.GetAsInt());
                ObjectIDMap.erase(id);
            }
            
            ObjectID GenerateNewID() {
                if (!availableIDs.empty()) {
                    int id = *availableIDs.begin();
                    availableIDs.erase(availableIDs.begin());
                    return ObjectID(id);
                }
                
                return GenerateNextID();
            }

            void AssignID(ObjectID id, std::shared_ptr<Object> obj){
                ObjectIDMap[id] = obj;
            }
        
            std::shared_ptr<Object> GetObjectFromID(ObjectID id){
                auto it = ObjectIDMap.find(id);
                if (it != ObjectIDMap.end()) {
                    return it->second;
                }
                return nullptr;
            } 

        private:
            std::atomic<int> nextId{1};
            ObjectID GenerateNextID() {
                int val = nextId.fetch_add(1);
                return ObjectID(val);
            }
    
            std::map<ObjectID, std::shared_ptr<Object>> ObjectIDMap;
            std::unordered_set<int> availableIDs;
    };
}

namespace std {
    template<>
    struct hash<Shard::Engine::Core::ObjectID> {
        std::size_t operator()(const Shard::Engine::Core::ObjectID& id) const noexcept {
            return std::hash<int>{}(id.GetAsInt());
        }
    };
}