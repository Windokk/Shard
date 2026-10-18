#include "gun.hpp"

#include "engine/platform/devices/iinput.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/actor.hpp"
#include "engine/renderer/components/model_component.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/world/time_manager.hpp"

using namespace Shard::Engine;

void Gun::OnTick()
{
    Shard::Engine::Core::Platform::IInput* input = Shard::Engine::Core::GetEngine().GetInputManager();
    if(input->WasMousePressed(Input::MouseButton::Left)){
        Shoot(bulletLifetime);
    }

    // The bullets sleep for their lifetime, then go
    const float dt = Shard::Engine::Core::GetEngine().GetTimeManager()->GetDeltaTime();
    for (auto it = bullets.begin(); it != bullets.end();)
    {
        auto bullet = it->actor.lock();
        it->remaining -= dt;
        if (!bullet)
            it = bullets.erase(it);
        else if (it->remaining <= 0.f)
        {
            bullet->Destroy();
            it = bullets.erase(it);
        }
        else
            ++it;
    }
}

void Gun::Shoot(float time)
{
    Shard::Engine::Worlds::World* world = Shard::Engine::Core::GetEngine().GetWorldManager()->GetWorldAt(0);
    if (!world)
        return;

    if (!parent)
        return;

    auto actor = Shard::Engine::Core::Object::CreateWithContext<Actor>(&Shard::Engine::Core::GetEngine(), "Bullet", &Shard::Engine::Core::GetEngine());
    // At the root of the world : a child of the gun's actor would follow it instead of flying away
    world->AddActor(actor);

    const glm::vec3 forward = parent->transform->GetWorldForward();
    const glm::quat aim = parent->transform->GetWorldRotationQuat();

    actor->transform->SetWorldPositionD(parent->transform->GetWorldPositionD() + glm::dvec3(forward) * 1.0);
    // The capsule lies along its local Y : lay it down along the shot direction (-Z)
    actor->transform->SetRotation(aim * glm::angleAxis(glm::radians(90.f), glm::vec3(1.f, 0.f, 0.f)));

    std::shared_ptr<Model> model = actor->AddComponent<Model>();
    model->SetMesh(mesh);
    auto* assetIDs = Shard::Engine::Core::GetEngine().GetAssetIDManager();
    if (auto matAsset = assetIDs->GetAssetFromID(material))
    {
        auto mat = Shard::Engine::Core::GetEngine().GetResourcesManager()->Get<Rendering::Material>(Shard::Engine::Core::Resources::AssetKind::Material, matAsset->baseInfos.nameInProject);
        if (mat)
            model->SetMaterials(std::vector<std::shared_ptr<Rendering::Material>>{mat});
    }

    auto physics_bod = actor->AddComponent<Shard::Engine::Objects::Components::PhysicsBody>();
    physics_bod->SetShapeType(0, Shard::Engine::Physics::PhysicsShape::CAPSULE);
    // Dynamic from the start, and created now so the impulse has a body to push
    physics_bod->CreateBody(JPH::EMotionType::Dynamic);
    // A speed, not an impulse : the capsule weighs over a ton (Jolt density 1000 kg/m3), an impulse of a few hundred barely moves it
    physics_bod->SetLinearVelocity(forward * gunShotIntensity);

    bullets.push_back({actor, time});
}

void Gun::OnPlay()
{

}
REGISTER_COMPONENT(Gun);
