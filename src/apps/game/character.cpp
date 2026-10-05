#include "character.hpp"

#include "engine/world/actor.hpp"

#include "engine/core/engine.hpp"

#include "engine/platform/iplatform.hpp"

#include "engine/time/time_manager.hpp"

#include <thread>
#include <iostream>

#include "engine/world/components/script.reflection.hpp"

using namespace Shard::Engine;

void Character::Deserialize(const json componentData) {
    // Deserialize fields
    if(componentData.contains("active") && componentData["active"].is_boolean() && componentData["active"]){
        Activate();
    }
    else{
        DeActivate();
    }
}

ordered_json Character::Serialize() {
    
    ordered_json comp;

    comp["type"] = "Character";

    comp["active"] = activated;

    return comp;
}

void Character::OnPlay() {
    glm::vec3 forward = parent->transform->GetForward();
    pitch = glm::degrees(asinf(glm::clamp(forward.y, -1.0f, 1.0f)));
    yaw = glm::degrees(atan2f(-forward.x, -forward.z));
}

void Character::OnTick() {
    Core::Platform::IInput* input = Core::GetEngine().GetInputManager();

    // Scripts tick once per rendered frame, so movement is expressed in units per second and scaled
    // by the frame's delta - translating by a flat amount would make the character move faster the
    // higher the framerate. Mouse look is deliberately not scaled: a cursor delta is already an
    // amount of movement, not a rate.
    const float dt = Core::GetEngine().GetTimeManager()->GetDeltaTime();
    const float step = speed * dt;

    if(input->IsKeyDown(Input::Key::W)){
        parent->transform->Translate(parent->transform->GetForward() * step);
    }
    if(input->IsKeyDown(Input::Key::A)){
        parent->transform->Translate(glm::normalize(glm::cross(parent->transform->GetForward(), parent->transform->GetUp())) * -step);
    }
    if(input->IsKeyDown(Input::Key::S)){
        parent->transform->Translate(parent->transform->GetForward() * -step);
    }
    if(input->IsKeyDown(Input::Key::D)){
        parent->transform->Translate(glm::normalize(glm::cross(parent->transform->GetForward(), parent->transform->GetUp())) * step);
    }

    if (input->IsMouseDown(Input::MouseButton::Left))
    {
        input->SetCursorVisibility(Shard::Engine::Core::Platform::CursorVisibility::Disabled);

        double mouseX, mouseY;
        input->GetCursorPos(&mouseX, &mouseY);

        if (firstClick)
        {
            lockedMouseX = mouseX;
            lockedMouseY = mouseY;
            firstClick = false;
        }

        double deltaX = mouseX - lockedMouseX;
        double deltaY = lockedMouseY - mouseY; // reversed Y

        pitch += deltaY * mouseSensitivity;
        yaw   -= deltaX * mouseSensitivity;

        // Clamp pitch to avoid flipping
        pitch = glm::clamp(pitch, -89.0f, 89.0f);

        // Build quaternion from yaw * pitch
        glm::quat qPitch = glm::angleAxis(glm::radians(pitch), glm::vec3(1, 0, 0));
        glm::quat qYaw   = glm::angleAxis(glm::radians(yaw),   glm::vec3(0, 1, 0));
        glm::quat rotation = qYaw * qPitch;
        
        parent->transform->SetRotation(rotation);
 
        // Reset cursor back to locked position every frame
        input->SetCursorPos(lockedMouseX, lockedMouseY);
    }
    if(input->IsMouseUp(Input::MouseButton::Left))
    {
        firstClick = true;
        input->SetCursorVisibility(Shard::Engine::Core::Platform::CursorVisibility::Visible);
    }
}

void Character::OnStop()
{
    Core::GetEngine().GetInputManager()->SetCursorVisibility(Shard::Engine::Core::Platform::CursorVisibility::Visible);
}

REGISTER_COMPONENT(Character);