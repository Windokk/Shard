#pragma once

#include <vector>
#include <memory>

#include "engine/world/object.hpp"


namespace Shard::Engine::Objects{

    class WorldObject : public Core::Object{
        public:

            virtual ~WorldObject();

            std::shared_ptr<WorldObject> GetChild(int index);

            std::shared_ptr<WorldObject> GetChild(Core::ObjectID id);
            std::vector<Core::ObjectID> GetChildrenID(bool recursive = false){ 
                std::vector<Core::ObjectID> ids;

                ids.insert(ids.end(), children.begin(), children.end());
                
                if (recursive) {
                    for (const auto& childID : children) {
                        std::shared_ptr<WorldObject> child = GetChild(childID);
                        if (child) {
                            std::vector<Core::ObjectID> subChildren = child->GetChildrenID(true);
                            ids.insert(ids.end(), subChildren.begin(), subChildren.end());
                        }
                    }
                }
                
                return ids;
            }
            int GetChildrenCount() { return children.size(); }

            virtual void AddChild(std::shared_ptr<WorldObject> o);

            void DeleteChildRef(Core::ObjectID child){
                for(int i = 0; i < children.size(); i++){
                    if(children[i].GetAsInt() == child.GetAsInt()){
                        children.erase(children.begin()+i);
                    }
                }
            }

            std::shared_ptr<WorldObject> GetParent();
            void SetParent(Core::ObjectID parentID) { this->parent = parentID; }

            Core::ObjectID GetID() { return id; }

            void Destroy() override;
        
        protected:
            std::vector<Core::ObjectID> children;
            Core::ObjectID parent = Core::ObjectID(-1);
    };
}
