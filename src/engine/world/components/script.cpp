#include "script.hpp"

#include "script.reflection.hpp"

namespace Shard::Engine::Objects::Components
{
    Script::Script(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {    
    }

    void Script::Destroy()
    {
        this->OnDestroyed();
    }

    void Script::OnLevelLoaded()
    {
    }

    void Script::OnLevelUnloaded()
    {
    }

    void Script::OnCreate()
    {
    }

    void Script::OnPlay()
    {
    }

    void Script::OnTick()
    {
    }

    void Script::OnStop()
    {
    }

    void Script::OnDestroyed()
    {
    }

    void Script::OnContactAdded(const Events::ContactAddedEvent& event)
    {
    }

    void Script::OnContactPersisted(const Events::ContactPersistedEvent& event)
    {
    }

    void Script::OnContactEnded(const Events::ContactRemovedEvent& event)
    {
    }

    void Script::OnActivated()
    {
    }

    void Script::OnDeactivated()
    {

    }
    
    std::shared_ptr<Component> Script::Clone() const
    {
        auto cloned = Object::Create<Script>(*this);

        return cloned;
    }
}
