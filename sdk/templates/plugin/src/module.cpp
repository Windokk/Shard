// The plugin itself: its entry point, and what is not a script, a component or a system: console variables, commands, content, hot reload.
// The scripts, components and systems of the other files register themselves (see gameplay/ and ecs/), so adding one never touches this file.
#include "module.hpp"

shard::CVar<float>& SpinScale()
{
    static shard::CVar<float> scale;
    return scale;
}

class TemplateModule : public shard::Plugin {
public:
    void OnLoad(shard::Context& ctx) override
    {
        // "<plugin id>.scale" in the console and the config files
        SpinScale() = ctx.CVars().Float("scale", 1.f, "multiplies every speed", 0.f, 10.f);
        ctx.Log("loaded");
    }

    void OnContentMounted(shard::Context& ctx) override
    {
        if (auto text = ctx.Content().Read(ctx.Content().UriOf("readme.txt")))
            ctx.Log("content : " + std::to_string(text->size()) + " bytes in readme.txt");
    }

    // Hot reload (in the editor) : what survives the new binary
    bool OnPrepareReload(shard::Context&, shard::StateWriter& state) override { return state.Set("scale", std::to_string(SpinScale().Get())); }
    void OnRestore(shard::Context&, shard::StateReader& state) override
    {
        if (auto value = state.Get("scale")) SpinScale().Set(std::stof(*value));
    }
};

SHARD_C_PLUGIN(TemplateModule)
