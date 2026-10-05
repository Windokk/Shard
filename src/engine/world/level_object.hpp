#pragma once

#include <vector>
#include <memory>

#include "engine/core/object.hpp"


namespace Shard::Engine::Objects{

    class LevelObject : public Core::Object{
        public:

            virtual ~LevelObject();

            std::shared_ptr<LevelObject> GetChild(int index);

            std::shared_ptr<LevelObject> GetChild(Core::ObjectID id);
            std::vector<Core::ObjectID> GetChildrenID(bool recursive = false){ 
                std::vector<Core::ObjectID> ids;

                ids.insert(ids.end(), children.begin(), children.end());
                
                if (recursive) {
                    for (const auto& childID : children) {
                        std::shared_ptr<LevelObject> child = GetChild(childID);
                        if (child) {
                            std::vector<Core::ObjectID> subChildren = child->GetChildrenID(true);
                            ids.insert(ids.end(), subChildren.begin(), subChildren.end());
                        }
                    }
                }
                
                return ids;
            }
            int GetChildrenCount() { return children.size(); }

            virtual void AddChild(std::shared_ptr<LevelObject> o);

            void DeleteChildRef(Core::ObjectID child){
                for(int i = 0; i < children.size(); i++){
                    if(children[i].GetAsInt() == child.GetAsInt()){
                        children.erase(children.begin()+i);
                    }
                }
            }

            std::shared_ptr<LevelObject> GetParent();
            void SetParent(Core::ObjectID parentID) { this->parent = parentID; }

            Core::ObjectID GetID() { return id; }

            void Destroy() override;
        
        protected:
            std::vector<Core::ObjectID> children;
            Core::ObjectID parent = Core::ObjectID(-1);
    };
}
