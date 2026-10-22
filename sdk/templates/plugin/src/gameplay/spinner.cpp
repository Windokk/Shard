#include "spinner.hpp"

void Spinner::OnTick(float deltaTime)
{
    const float angle = Shard::Radians(degreesPerSecond * SpinScale().Get() * deltaTime);
    Owner().SetRotation(Shard::AxisAngle(axis, angle) * Owner().Rotation());
}

// Registers the class: this is all that connects it to the plugin
SHARD_REGISTER_SCRIPT(Spinner)
