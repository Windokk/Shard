#pragma once

#include <string>
#include <atomic>
#include <unordered_set>
#include <map>
#include <memory>
#include <engine/core/diagnostics/logger.hpp>

namespace Shard::Engine::Filesystem
{
    struct AssetInfos;

    class Path;

    class AssetID {
        public:
            
            AssetID() : packed(0) {}
            explicit AssetID(int value) : packed(value) {}

            
            int GetAsInt() const {
                return packed;
            }

            std::string GetAsString() const {
                return std::to_string(GetAsInt());
            }

            
            bool operator==(const AssetID& other) const { return packed == other.packed; }
            bool operator!=(const AssetID& other) const { return this->packed != other.packed; }
            bool operator<(const AssetID& other) const { return packed < other.packed; }

            
            friend class AssetIDBuilder;
    
        private:
            int packed;
    };

    struct FileInfos; 

    class AssetIDManager {
        public:
            void InitCounter(int startID) {
                m_nextId.store(startID);
                m_initialized = true;
            }

            void Reset() {
                AssetIDMap.clear();
                availableIDs.clear();
                m_initialized = false;
                m_nextId.store(0);
            }

            AssetID GenerateNewID() {
                if (!m_initialized) {
                    DEBUG_ERROR("Called GenerateNewID() before InitCounter()");
                    return AssetID(-1);
                }
                if (!availableIDs.empty()) {
                    auto it = availableIDs.begin();
                    AssetID recycled(*it);
                    availableIDs.erase(it);
                    return recycled;
                }
                return AssetID(m_nextId.fetch_add(1));
            }

            AssetID GenerateNewIDWithValue(int val) {
                return AssetID(val);
            }

            void DestroyID(const AssetID& id);
            void AssignID(AssetID id, std::shared_ptr<AssetInfos> info);

            std::shared_ptr<AssetInfos> GetAssetFromID(AssetID id);
            AssetID GetIDFromNameInProject(const std::string nameInProject);

            std::map<AssetID, std::shared_ptr<AssetInfos>> AssetIDMap;

        private:
            std::atomic<int>  m_nextId{0};
            bool m_initialized = false;
            std::unordered_set<int> availableIDs;
    };
}
