#include "module.hpp"

Shard::CVar<float>& SpinScale()
{
    static Shard::CVar<float> scale;
    return scale;
}

class TemplateModule : public Shard::Plugin {
public:
    void OnLoad(Shard::Context& ctx) override
    {
        SpinScale() = ctx.CVars().Float("scale", 1.f, "multiplies every speed", 0.f, 10.f);
        ctx.Log("loaded");
    }

    void OnContentMounted(Shard::Context& ctx) override
    {
        if (auto text = ctx.Content().Read(ctx.Content().UriOf("readme.txt")))
            ctx.Log("content : " + std::to_string(text->size()) + " bytes in readme.txt");
    }

    bool OnPrepareReload(Shard::Context&, Shard::StateWriter& state) override { return state.Set("scale", std::to_string(SpinScale().Get())); }
    void OnRestore(Shard::Context&, Shard::StateReader& state) override
    {
        if (auto value = state.Get("scale")) SpinScale().Set(std::stof(*value));
    }
};

SHARD_C_PLUGIN(TemplateModule)
