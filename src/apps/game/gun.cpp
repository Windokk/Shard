#include "gun.hpp"

#include "bullet.hpp"

void Gun::OnTick(float deltaTime)
{
    if (shard::Input::WasMousePressed(shard::MouseButton::Left))
        Shoot(bulletLifetime);

    // The bullets sleep for their lifetime, then go
    for (auto it = bullets.begin(); it != bullets.end();)
    {
        it->remaining -= deltaTime;
        if (!it->actor.IsValid())
            it = bullets.erase(it);
        else if (it->remaining <= 0.f)
        {
            it->actor.Destroy();
            it = bullets.erase(it);
        }
        else
            ++it;
    }
}

void Gun::Shoot(float time)
{
    // At the root of the world : a child of the gun's actor would follow it instead of flying away
    shard::Actor bullet = GetScene().Create("Bullet");

    const shard::Pose gun = Owner().WorldPose();
    const shard::Vec3 forward = gun.Forward();
    shard::Pose pose;
    pose.position = gun.position + forward * 1.0f;
    // The capsule lies along its local Y : lay it down along the shot direction (-Z)
    pose.rotation = gun.rotation * shard::AxisAngle({ -1.f, 0.f, 0.f }, shard::Radians(90.f));
    bullet.SetWorldPose(pose);

    shard::Audio::PlayOneShot("sounds/Gun SFX.mp3", Owner().WorldPose().position);

    try
    {
        auto model = bullet.Add<shard::Model>();
        if (mesh)
            model.SetMesh(mesh);
        if (material)
            model.SetMaterial(0, material);

        auto body = bullet.Add<shard::PhysicsBody>();

        body.SetShape(shard::Shape::Capsule().At(shard::Vec3{0, capsuleYOffset, 0}));
        // The body is Dynamic as soon as this returns, so the velocity has something to push. A speed, not an impulse : the capsule weighs
        // over a ton (Jolt density 1000 kg/m3), an impulse of a few hundred barely moves it
        body.SetMotionType(shard::MotionType::Dynamic);
        body.SetGravityMode(shard::GravityMode::Custom);
        body.SetCustomGravity(shard::Vec3{0, 0, 0});
        bullet.SetVelocity(forward * gunShotIntensity);
        bullet.AddScript<Bullet>();
    }
    catch (...)
    {
        bullet.Destroy();       // no half-built bullet left in the world
        throw;
    }

    bullets.push_back({ bullet, time });
}

SHARD_REGISTER_SCRIPT(Gun)
