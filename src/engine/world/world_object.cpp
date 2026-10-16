#include "world_object.hpp"

#include "engine/core/diagnostics/logger.hpp"

#include "engine/world/engine.hpp"

#include <memory>

namespace Shard::Engine::Objects{
    
    std::shared_ptr<WorldObject> WorldObject::GetParent()
    {
        auto obj = Core::GetEngine().GetObjectIDManager()->GetObjectFromID(parent);
        if(auto worldObj = std::dynamic_pointer_cast<WorldObject>(obj))
            return worldObj;
        else
            return nullptr;
    }

    void WorldObject::Destroy()
    {
        if(parent.GetAsInt() != -1){
            if(auto parentObject = GetParent())
                parentObject->DeleteChildRef(id);
        }

        // Taken out first : each child removes itself from this list while it is destroyed (DeleteChildRef above),
        // which skipped every other child when it was iterated directly - they were left alive, hanging off a dead parent
        std::vector<Core::ObjectID> ownChildren = std::move(children);
        children.clear();
        for(auto& childID : ownChildren)
        {
            auto child = std::dynamic_pointer_cast<WorldObject>(Core::GetEngine().GetObjectIDManager()->GetObjectFromID(childID));
            if(child)
                child->Destroy();
        }

        Object::Destroy();
    }

    WorldObject::~WorldObject()
    {

    }

    std::shared_ptr<WorldObject> WorldObject::GetChild(int index)
    {
        auto obj = Core::GetEngine().GetObjectIDManager()->GetObjectFromID(children[index]);
        if(auto worldObj = std::dynamic_pointer_cast<WorldObject>(obj))
            return worldObj;
        else
            return nullptr;
    }

    std::shared_ptr<WorldObject> WorldObject::GetChild(Core::ObjectID ObjectID)
    {
        if (GetID() == ObjectID)
            return AsShared<WorldObject>();

        for (auto& child : children)
        {
            if (child == ObjectID) {
                auto obj = Core::GetEngine().GetObjectIDManager()->GetObjectFromID(child);
                if(auto worldObj = std::dynamic_pointer_cast<WorldObject>(obj))
                    return worldObj;
                else
                    return nullptr;
            }
        }

        return nullptr;
    }

    void WorldObject::AddChild(std::shared_ptr<WorldObject> o)
    {
        children.push_back(o->GetID());
        o->SetParent(id);
    }
}