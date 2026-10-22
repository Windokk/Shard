#include "character.hpp"

#include <cmath>

void Character::OnPlay()
{
    const shard::Vec3 facing = Owner().LocalPose().Forward();
    pitch = shard::Degrees(std::asin(shard::Clamp(facing.y, -1.0f, 1.0f)));
    yaw = shard::Degrees(std::atan2(-facing.x, -facing.z));

    shard::Input::SetCursorMode(shard::CursorMode::Disabled);
}

void Character::OnTick(float deltaTime)
{
    // Scripts tick once per rendered frame, so movement is expressed in units per second and scaled by the frame's delta : moving by a flat
    // amount would make the character go faster the higher the framerate. Mouse look is deliberately not scaled : a cursor delta is already
    // an amount of movement, not a rate.
    const float step = speed * deltaTime;

    shard::Pose pose = Owner().LocalPose();
    const shard::Vec3 forward = pose.Forward();
    const shard::Vec3 right = pose.Right();
    bool moved = false;
    if (shard::Input::IsKeyDown(shard::Key::W)) { pose.position += forward * step; moved = true; }
    if (shard::Input::IsKeyDown(shard::Key::A)) { pose.position += right * -step; moved = true; }
    if (shard::Input::IsKeyDown(shard::Key::S)) { pose.position += forward * -step; moved = true; }
    if (shard::Input::IsKeyDown(shard::Key::D)) { pose.position += right * step; moved = true; }
    if (moved)
        Owner().SetLocalPose(pose);

    if (shard::Input::IsMouseDown(shard::MouseButton::Left))
    {
        const shard::Vec2 cursor = shard::Input::CursorPosition();

        if (firstClick)
        {
            lockedMouseX = cursor.x;
            lockedMouseY = cursor.y;
            firstClick = false;
        }

        const double deltaX = cursor.x - lockedMouseX;
        const double deltaY = lockedMouseY - cursor.y;     // reversed Y

        pitch += float(deltaY * mouseSensitivity);
        yaw -= float(deltaX * mouseSensitivity);

        // Clamp pitch to avoid flipping
        pitch = shard::Clamp(pitch, -89.0f, 89.0f);

        // yaw about the vertical, then pitch about the right axis
        Owner().SetRotation(shard::FromEulerDegrees(pitch, yaw, 0.f));

        // Reset cursor back to locked position every frame
        shard::Input::SetCursorPosition(lockedMouseX, lockedMouseY);
    }
    else
        firstClick = true;
}

SHARD_REGISTER_SCRIPT(Character)
