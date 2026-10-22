#include "tutorial_player.hpp"

#include "health.hpp"                       // a script of another file : nothing to link, just a header

void Bullet::OnTick(float deltaTime)
{
    life -= deltaTime;
    if (life <= 0.f) Owner().Destroy();
}
SHARD_REGISTER_SCRIPT(Bullet, (life))

void Player::OnTick(float dt)
{
    const float turn = Shard::Input::Axis(Shard::Key::A, Shard::Key::D);
    const float forward = Shard::Input::Axis(Shard::Key::S, Shard::Key::W);
    Owner().Translate(Owner().Forward() * (forward * speed * dt));
    Owner().SetRotation(Shard::AxisAngle({ 0.f, 1.f, 0.f }, -turn * 2.f * dt) * Owner().Rotation());

    Shard::Scene scene = GetScene();                        // the world this actor is in
    if (Shard::Input::WasPressed(Shard::Key::Space)) {
        Shard::Actor bullet = scene.Create("Bullet");
        bullet.SetWorldPose(Owner().WorldPose());
        bullet.AddComponent("light", R"({ "intensity": 3.0 })");        // the engine's own components, by the name a world file gives them
        bullet.AddScript<Bullet>();                                       // and the scripts of this plugin
    }
    if (Shard::Actor enemy = scene.Find("Boss"))
        if (Health* health = enemy.GetScript<Health>())
            health->Damage(10.f);
}
SHARD_REGISTER_SCRIPT(Player, (speed))
