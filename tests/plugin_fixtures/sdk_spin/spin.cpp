// The plugin of docs/design-notes/c-abi-sdk.fr.md (7.2), written with the C++ SDK of sdk/include/shard/cpp : no engine header, nothing of the
// engine linked, C++17. Loaded by tests/plugins/sdk_spin_tests.cpp ; the exported spin_* functions let the test look inside.
#include <string>

#include <shard/cpp/plugin.hpp>

#if defined(_WIN32)
#   define TEST_EXPORT extern "C" __declspec(dllexport)
#else
#   define TEST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

struct Spin {
    float speed = 1.f;
    float angle = 0.f;
    shard::Vec3 axis{ 0.f, 1.f, 0.f };
    int32_t turns = 0;
};
SHARD_COMPONENT(Spin, (speed)(angle)(axis)(turns))

// ABI 1.3 : a text and an asset in a component, and a system that moves the actor
struct Sign {
    shard::Text text;
    shard::AssetId icon;
    int32_t uses = 0;
};
SHARD_COMPONENT(Sign, (text)(icon)(uses))

// The payload of "shard.test.spin.Turned" : described for the tools (event.watch)
struct Turned {
    int32_t turns = 0;
    float speed = 0.f;
};
SHARD_COMPONENT(Turned, (turns)(speed))

namespace {
    int gTurnEvents = 0;
    std::string gLastError;
}

class SpinPlugin : public shard::Plugin {
public:
    void OnLoad(shard::Context& ctx) override
    {
        auto spin = ctx.Components().Register<Spin>("Spin", {
            { "speed", 0, 0.f, 20.f, "radians per second" },
            { "turns", SHARD_FIELD_RUNTIME, 0.f, 0.f, "" },
        });
        m_Scale = ctx.CVars().Float("scale", 1.f, "multiplies every speed", 0.f, 10.f);
        const shard::CVar<float> scale = m_Scale;
        ctx.Systems().Add("rotate", shard::Phase::Update)
            .Writes(spin)
            .Run([spin, scale](shard::SystemCtx& s) {
                const float k = scale.Get();
                s.Each(spin, [&](Spin& c) {
                    c.angle += c.speed * k * s.DeltaTime();
                    while (c.angle >= 6.2831853f) {
                        c.angle -= 6.2831853f;
                        ++c.turns;
                        s.Emit("shard.test.spin.Turned", Turned{ c.turns, c.speed });
                    }
                });
            });
        auto sign = ctx.Components().Register<Sign>("Sign");
        auto pose = ctx.Components().Find<shard::Pose>("shard.LocalTransform");
        ctx.Systems().Add("sign", shard::Phase::Update)
            .Writes(sign, pose)
            .Run([sign](shard::SystemCtx& s) {
                s.Each(sign, [&](shard::Entity e, Sign& c) {
                    ++c.uses;
                    s.SetText(c.text, s.GetText(c.text) + "*");
                    shard::Pose p;
                    p.position.x = float(c.uses);
                    p.scale = { 3.f, 3.f, 3.f };
                    s.SetLocalTransform(e, p);
                });
            });
        ctx.Events().Describe<Turned>("shard.test.spin.Turned");
        ctx.Events().Subscribe<Turned>("shard.test.spin.Turned", [](const Turned&) { ++gTurnEvents; });
        ctx.Commands().Add("spin.scale", "spin.scale <k> : sets the scale", [this](const std::vector<std::string_view>& args, std::string& out) {
            if (args.empty()) { out = "usage: spin.scale <k>"; return false; }
            m_Scale.Set(std::stof(std::string(args[0])));
            out = "scale " + std::to_string(m_Scale.Get());
            return true;
        });
        // shard.experimental.world_ecs : the Spin entities, counted from the main thread
        const shard::Context here = ctx;
        ctx.Commands().Add("spin.count", "spin.count : how many entities spin", [here, spin](const std::vector<std::string_view>&, std::string& out) {
            shard::experimental::World world(here.Handle());
            out = std::to_string(world.Ecs().Query(spin).Count());
            return true;
        });
        // A registration the host refuses throws shard::Error ; caught here to show it does not take the plugin down
        try {
            ctx.Hooks().On("NoSuchHook", [](shard::Hook&) {});
        } catch (const shard::Error& e) {
            gLastError = e.what();
        }
        ctx.Hooks().On("PreSave", [](shard::Hook& hook) {
            if (hook.Subject() == "spin.world") hook.Cancel("spinning");
        });
    }

    bool OnPrepareReload(shard::Context&, shard::StateWriter& state) override
    {
        return state.Set("scale", std::to_string(m_Scale.Get()));
    }

    void OnRestore(shard::Context&, shard::StateReader& state) override
    {
        if (auto value = state.Get("scale")) m_Scale.Set(std::stof(*value));
    }

private:
    shard::CVar<float> m_Scale;
};

SHARD_C_PLUGIN(SpinPlugin)

TEST_EXPORT int spin_turn_events() { return gTurnEvents; }
TEST_EXPORT const char* spin_last_error() { return gLastError.c_str(); }
