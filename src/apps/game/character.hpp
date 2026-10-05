#pragma once

#include "engine/world/components/script.hpp"
#include "engine/world/components/registry/component_macros.hpp"

using namespace nlohmann;

class CLASS() Character : public Shard::Engine::Objects::Components::Script{

    public:
        Character(std::shared_ptr<Actor> parent, uint32_t local_id) : Script(parent, local_id){};

        void Deserialize(const json componentData) override;
        ordered_json Serialize() override;

        void OnPlay() override;
        void OnTick() override;
        void OnStop() override;

        FIELD(Editable)
        float speed = 30.0f; // units per second
        
        FIELD(Editable)
        float mouseSensitivity = 0.1f;
        
        FIELD(ReadOnly)
        double lockedMouseX = 0; 
        
        FIELD(ReadOnly)
        double lockedMouseY = 0;
        
        FIELD(ReadOnly)
        bool firstClick = true;

        FIELD(ReadOnly)
        float pitch = 0.0f;
        
        FIELD(ReadOnly)
        float yaw = 0.0f;

    private:

        DECLARE_DESCRIPTOR(Character);
};

DECLARE_COMPONENT(Character)