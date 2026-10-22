#include "gun.hpp"

#include "bullet.hpp"

void Gun::OnTick(float deltaTime)
{
    if (Shard::Input::WasMousePressed(Shard::MouseButton::Left))
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
    Shard::Actor bullet = GetScene().Create("Bullet");

    const Shard::Pose gun = Owner().WorldPose();
    const Shard::Vec3 forward = gun.Forward();
    Shard::Pose pose;
    pose.position = gun.position + forward * 1.0f;
    // The capsule lies along its local Y : lay it down along the shot direction (-Z)
    pose.rotation = gun.rotation * Shard::AxisAngle({ -1.f, 0.f, 0.f }, Shard::Radians(90.f));
    bullet.SetWorldPose(pose);

    try
    {
        auto model = bullet.Add<Shard::Model>();
        if (mesh)
            model.SetMesh(mesh);
        if (material)
            model.SetMaterial(0, material);

        auto body = bullet.Add<Shard::PhysicsBody>();

        body.SetShape(Shard::Shape::Capsule().At(Shard::Vec3{0, capsuleYOffset, 0}));
        // The body is Dynamic as soon as this returns, so the velocity has something to push. A speed, not an impulse : the capsule weighs
        // over a ton (Jolt density 1000 kg/m3), an impulse of a few hundred barely moves it
        body.SetMotionType(Shard::MotionType::Dynamic);
        body.SetGravityMode(Shard::GravityMode::Custom);
        body.SetCustomGravity(Shard::Vec3{0, 0, 0});
        bullet.SetVelocity(forward * gunShotIntensity);
        bullet.AddScript<Bullet>();
    }
    catch (...)
    {
        bullet.Destroy();       // no half-built bullet left in the world
        throw;
    }

    // a host with no audio (a server, the tests) or a project without the clip still gets its bullet
    try
    {
        Shard::Audio::PlayOneShot("sounds/Gun SFX.mp3", gun.position);
    }
    catch (const Shard::Error&)
    {
    }

    bullets.push_back({ bullet, time });
}

SHARD_REGISTER_SCRIPT(Gun)
