// What a script does with the engine's components through the typed handles (shard/cpp/engine_components.hpp)
#include <shard/game.hpp>

#include "stats.hpp"

class Builder : public Shard::Script {
public:
    void OnPlay() override
    {
        Shard::Actor made = GetScene().Create("Built");
        made.SetPosition({ 0.f, 3.f, 0.f });

        auto light = made.Add<Shard::Light>();
        light.SetType(Shard::LightType::Spot);
        light.SetIntensity(7.5f);
        light.SetColor({ 0.25f, 0.5f, 1.f, 1.f });
        light.SetCone(10.f, 30.f);
        light.SetCastShadows(false);
        GetStats().lightIntensity = light.Intensity();          // read back through the same handle

        auto model = made.Add<Shard::Model>();
        model.SetStaticLighting(false);
        model.SetLightmapResolution(256);

        auto body = made.Add<Shard::PhysicsBody>();
        body.SetShape(Shard::Shape::Sphere(0.25f));
        body.AddShape(Shard::Shape::Box({ 0.1f, 0.2f, 0.3f }).At({ 0.f, 1.f, 0.f }));
        GetStats().shapeCount = int(body.ShapeCount());
        body.RemoveShape(1);
        GetStats().shapeCountAfterRemove = int(body.ShapeCount());
        body.SetMotionType(Shard::MotionType::Dynamic);
        body.SetFriction(0.75f);
        body.SetMass(12.f);
        // The body of the new motion type exists as soon as it is set, and changing a field does not rebuild it : the velocity has something
        // to push, and keeps pushing
        GetStats().velocitySet = made.SetVelocity({ 1.f, 2.f, 3.f }) ? 1 : 0;
        body.SetFriction(0.5f);

        // A typo is an error of the script, not a silent nothing
        try {
            light.SetFloat("intensty", 1.f);
        } catch (const Shard::Error&) {
            ++GetStats().typedRefused;
        }
        try {
            body.SetJson("friction", "\"slippery\"");           // not a number
        } catch (const Shard::Error&) {
            ++GetStats().typedRefused;
        }

        // Has, Get and Remove by type
        GetStats().noListener = made.Get<Shard::AudioListener>() ? 0 : 1;        // none yet : the handle says so
        made.Add<Shard::AudioListener>().SetDopplerFactor(2.5f);
        GetStats().hasListener = made.Has<Shard::AudioListener>() ? 1 : 0;
        GetStats().listenerDoppler = made.Get<Shard::AudioListener>().DopplerFactor();
        made.Remove<Shard::AudioListener>();
        GetStats().hasListenerAfterRemove = made.Has<Shard::AudioListener>() ? 1 : 0;
    }
};
SHARD_REGISTER_SCRIPT(Builder)
