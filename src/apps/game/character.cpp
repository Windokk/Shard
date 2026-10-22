#include "character.hpp"

#include <cmath>

void Character::OnPlay()
{
    const Shard::Vec3 facing = Owner().LocalPose().Forward();
    pitch = Shard::Degrees(std::asin(Shard::Clamp(facing.y, -1.0f, 1.0f)));
    yaw = Shard::Degrees(std::atan2(-facing.x, -facing.z));

    // The cursor is hidden and captured for the whole play session : the look does not wait for a click. The reference position is taken again
    // on the first tick, the cursor having moved freely since the previous session.
    Shard::Input::SetCursorMode(Shard::CursorMode::Disabled);
    firstFrame = true;
}

void Character::OnTick(float deltaTime)
{
    // Scripts tick once per rendered frame, so movement is expressed in units per second and scaled by the frame's delta : moving by a flat
    // amount would make the character go faster the higher the framerate. Mouse look is deliberately not scaled : a cursor delta is already
    // an amount of movement, not a rate.
    const float step = speed * deltaTime;

    Shard::Pose pose = Owner().LocalPose();
    const Shard::Vec3 forward = pose.Forward();
    const Shard::Vec3 right = pose.Right();
    bool moved = false;
    if (Shard::Input::IsKeyDown(Shard::Key::W)) { pose.position += forward * step; moved = true; }
    if (Shard::Input::IsKeyDown(Shard::Key::A)) { pose.position += right * -step; moved = true; }
    if (Shard::Input::IsKeyDown(Shard::Key::S)) { pose.position += forward * -step; moved = true; }
    if (Shard::Input::IsKeyDown(Shard::Key::D)) { pose.position += right * step; moved = true; }
    if (moved)
        Owner().SetLocalPose(pose);

    const Shard::Vec2 cursor = Shard::Input::CursorPosition();

    if (firstFrame)
    {
        lockedMouseX = cursor.x;
        lockedMouseY = cursor.y;
        firstFrame = false;
    }

    const double deltaX = cursor.x - lockedMouseX;
    const double deltaY = lockedMouseY - cursor.y;     // reversed Y

    pitch += float(deltaY * mouseSensitivity);
    yaw -= float(deltaX * mouseSensitivity);

    // Clamp pitch to avoid flipping
    pitch = Shard::Clamp(pitch, -89.0f, 89.0f);

    // yaw about the vertical, then pitch about the right axis
    Owner().SetRotation(Shard::FromEulerDegrees(pitch, yaw, 0.f));

    // Reset cursor back to locked position every frame
    Shard::Input::SetCursorPosition(lockedMouseX, lockedMouseY);
}

SHARD_REGISTER_SCRIPT(Character)
