#include "level_object.hpp"

#include "engine/debugging/logger.hpp"

#include "engine/core/engine.hpp"

#include <memory>

namespace Shard::Engine::Objects{
    
    std::shared_ptr<LevelObject> LevelObject::GetParent()
    {
        auto obj = Core::GetEngine().GetObjectIDManager()->GetObjectFromID(parent);
        if(auto lvlObj = std::dynamic_pointer_cast<LevelObject>(obj))
            return lvlObj;
        else
            return nullptr;
    }

    void LevelObject::Destroy()
    {
        if(parent.GetAsInt() != -1){
            GetParent()->DeleteChildRef(id);
        }
        for(auto& child : children)
        { 
            GetChild(child)->Destroy(); 
        }
        children.clear();

        Object::Destroy();
    }

    LevelObject::~LevelObject()
    {

    }

    std::shared_ptr<LevelObject> LevelObject::GetChild(int index)
    {
        auto obj = Core::GetEngine().GetObjectIDManager()->GetObjectFromID(children[index]);
        if(auto lvlObj = std::dynamic_pointer_cast<LevelObject>(obj))
            return lvlObj;
        else
            return nullptr;
    }

    std::shared_ptr<LevelObject> LevelObject::GetChild(Core::ObjectID ObjectID)
    {
        if (GetID() == ObjectID)
            return AsShared<LevelObject>();

        for (auto& child : children)
        {
            if (child == ObjectID) {
                auto obj = Core::GetEngine().GetObjectIDManager()->GetObjectFromID(child);
                if(auto lvlObj = std::dynamic_pointer_cast<LevelObject>(obj))
                    return lvlObj;
                else
                    return nullptr;
            }
        }

        return nullptr;
    }

    void LevelObject::AddChild(std::shared_ptr<LevelObject> o)
    {
        children.push_back(o->GetID());
        o->SetParent(id);
    }
}