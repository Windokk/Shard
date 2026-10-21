// A plugin written with the Shard SDK (sdk/include/shard/cpp) : a component, a system that updates it every frame, a variable of the console.
// Rename the id in shard-plugin.json, then make it yours.
#include <shard/cpp/plugin.hpp>

// A component : plain data. Its default member values are what a new one holds ; SHARD_COMPONENT says which members are saved and shown
// in the editor's inspector.
struct Spin {
    float speed = 1.f;          // radians per second
    float angle = 0.f;
    shard::Vec3 axis{ 0.f, 1.f, 0.f };
};
SHARD_COMPONENT(Spin, (speed)(angle)(axis))

class TemplatePlugin : public shard::Plugin {
public:
    void OnLoad(shard::Context& ctx) override
    {
        // Registered as "<plugin id>.Spin" : put it on an actor in the editor, or in a world file
        auto spin = ctx.Components().Register<Spin>("Spin", { { "speed", 0, 0.f, 20.f, "radians per second" } });

        // "<plugin id>.scale" in the console and the config files
        m_Scale = ctx.CVars().Float("scale", 1.f, "multiplies every speed", 0.f, 10.f);

        // Runs every frame, on the entities that have a Spin, in parallel with the systems it does not conflict with
        const shard::CVar<float> scale = m_Scale;
        ctx.Systems().Add("rotate", shard::Phase::Update)
            .Writes(spin)
            .Run([spin, scale](shard::SystemCtx& s) {
                const float k = scale.Get();
                s.Each(spin, [&](Spin& c) { c.angle += c.speed * k * s.DeltaTime(); });
            });

        ctx.Log("loaded");
    }

    void OnContentMounted(shard::Context& ctx) override
    {
        if (auto text = ctx.Content().Read(ctx.Content().UriOf("readme.txt")))
            ctx.Log("content : " + std::to_string(text->size()) + " bytes in readme.txt");
    }

    // Hot reload (in the editor) : what survives the new binary
    bool OnPrepareReload(shard::Context&, shard::StateWriter& state) override { return state.Set("scale", std::to_string(m_Scale.Get())); }
    void OnRestore(shard::Context&, shard::StateReader& state) override
    {
        if (auto value = state.Get("scale")) m_Scale.Set(std::stof(*value));
    }

private:
    shard::CVar<float> m_Scale;
};

SHARD_C_PLUGIN(TemplatePlugin)
