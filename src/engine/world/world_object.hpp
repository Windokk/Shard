#pragma once

#include <algorithm>
#include <vector>
#include <memory>

#include "engine/world/object.hpp"


namespace Shard::Engine::Objects{

    // Moves `item` right before (or after) `anchor` in a list of sibling ids. False when either is missing or they are the same.
    inline bool MoveIdNextTo(std::vector<Core::ObjectID>& list, Core::ObjectID item, Core::ObjectID anchor, bool after)
    {
        if (item.GetAsInt() == anchor.GetAsInt())
            return false;

        auto find = [&](Core::ObjectID id){
            return std::find_if(list.begin(), list.end(), [&](const Core::ObjectID& other){ return other.GetAsInt() == id.GetAsInt(); });
        };

        auto from = find(item);
        if (from == list.end() || find(anchor) == list.end())
            return false;

        const Core::ObjectID moved = *from;
        list.erase(from);

        auto to = find(anchor);
        list.insert(after ? to + 1 : to, moved);
        return true;
    }

    // Puts the ids of `order` first, in that order (the ones that are not in `list` are ignored), the other ids of `list` keep their order after them.
    inline void ApplyIdOrder(std::vector<Core::ObjectID>& list, const std::vector<Core::ObjectID>& order)
    {
        std::vector<Core::ObjectID> result;
        result.reserve(list.size());

        auto contains = [](const std::vector<Core::ObjectID>& ids, Core::ObjectID id){
            return std::any_of(ids.begin(), ids.end(), [&](const Core::ObjectID& other){ return other.GetAsInt() == id.GetAsInt(); });
        };

        for (const Core::ObjectID& id : order)
            if (contains(list, id) && !contains(result, id))
                result.push_back(id);

        for (const Core::ObjectID& id : list)
            if (!contains(result, id))
                result.push_back(id);

        list = std::move(result);
    }

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

            // Order among the siblings : the children are listed (and saved) in this order.
            bool MoveChildNextTo(Core::ObjectID child, Core::ObjectID anchor, bool after) { return MoveIdNextTo(children, child, anchor, after); }
            void SetChildrenOrder(const std::vector<Core::ObjectID>& order) { ApplyIdOrder(children, order); }

            virtual void AddChild(std::shared_ptr<WorldObject> o);

            void DeleteChildRef(Core::ObjectID child){
                for(size_t i = 0; i < children.size(); i++){
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
