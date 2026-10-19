// A plugin built as a real shared library, loaded by tests/plugins/dynamic_plugin_tests.cpp.
#include <fstream>
#include <iostream>
#include <sstream>

#include "engine/assets/asset_importer.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/plugins/context.hpp"
#include "engine/plugins/plugin.hpp"
#include "engine/renderer/frontend/pipelines/render_pipeline.hpp"
#include "engine/renderer/frontend/render_feature.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;

namespace {
    std::shared_ptr<Objects::Components::Component> MakeNothing() { return nullptr; }

    struct Pinger {
        int calls = 0;
        int Ping() { return ++calls; }
    };

    // A render feature that only says it was there
    class TestFeature final : public Rendering::RenderFeature {
    public:
        const char* GetName() const override { return "shard-test-feature"; }
        void Activate(Rendering::Renderer&) override { std::cout << "[TESTPLUGIN] feature activated" << std::endl; }
        void Deactivate(Rendering::Renderer&) override { std::cout << "[TESTPLUGIN] feature deactivated" << std::endl; }
        void BeforeFrame(Rendering::Renderer&, const Rendering::RenderSnapshot&) override { ++frames; }
        int frames = 0;
    };

    // A render pipeline with no passes : enough to be picked and dropped
    class TestPipeline final : public Rendering::RenderPipeline {
    public:
        const char* GetType() const override { return "shard-test-pipeline"; }
        void Activate(Rendering::Renderer&) override { std::cout << "[TESTPLUGIN] pipeline activated" << std::endl; }
        void Deactivate(Rendering::Renderer&) override { std::cout << "[TESTPLUGIN] pipeline deactivated" << std::endl; }
    };
}

class TestPlugin final : public IModule {
public:
    ModuleKind Kind() const override { return ModuleKind::Code; }

    void OnLoad(PluginContext& ctx) override
    {
        ctx.Components().Register("shard-test/Thing", &MakeNothing);
        m_Counter = ctx.CVars().Int("counter", 0, "Frames this plugin's system ran");
        m_Loads = ctx.CVars().Int("loads", 0, "How many times this library was loaded");
        ctx.CVars().String("note", "hello");

        ctx.Commands().Add("shardtest.ping", "pings the test plugin", [](const std::vector<std::string>&) {
            return Core::CommandResult{ true, "pong" };
        });

        Core::Ecs::SystemDesc tick;
        tick.name = "tick";
        tick.phase = Core::Ecs::Phase::Update;
        tick.run = [this](Core::Ecs::SystemContext&) {
            ++m_Frames;
            m_Counter->ForceSet(m_Frames);
        };
        ctx.Systems().Add(std::move(tick));

        ctx.Services().Provide<Pinger>("shardtest.pinger", 1, &m_Pinger);

        // Extension points the engine reads
        ctx.Render().AddFeature<Rendering::RenderFeature>("shard-test-feature", std::make_shared<TestFeature>());

        auto pipeline = std::make_shared<Rendering::RenderPipelineExtension>();
        pipeline->type = "shard-test-pipeline";
        pipeline->description = "A pipeline with no passes, from the test plugin";
        pipeline->factory = [](const nlohmann::json&) -> std::unique_ptr<Rendering::RenderPipeline> { return std::make_unique<TestPipeline>(); };
        ctx.Render().AddPipeline<Rendering::RenderPipelineExtension>("shard-test-pipeline", pipeline);

        auto type = std::make_shared<Core::Resources::AssetTypeExtension>();
        type->name = "shard-test-asset";
        type->extensions = { ".shardtest" };
        type->info.load = [](const std::string&, const Filesystem::AssetInfos&) -> std::shared_ptr<void> { return std::make_shared<int>(42); };
        m_AssetType = type;
        ctx.Assets().Add<Core::Resources::AssetTypeExtension>("asset.type", "shard-test-asset", type);

        auto importer = std::make_shared<Filesystem::AssetImporterExtension>();
        importer->name = "shard-test-importer";
        importer->extensions = { ".shardsrc" };
        importer->import = [](const std::string& source, const std::string& outputDirectory) {
            Filesystem::ImportResult result;
            std::ifstream in(source);
            std::stringstream text;
            text << in.rdbuf();
            std::string upper = text.str();
            for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
            const std::string out = outputDirectory + "/imported.txt";
            std::ofstream file(out);
            file << upper;
            result.ok = bool(file);
            result.message = "imported by the test plugin";
            result.outputs.push_back(out);
            return result;
        };
        ctx.Assets().Add<Filesystem::AssetImporterExtension>("asset.importer", "shard-test-importer", importer);

        ctx.Commands().Add("shardtest.assetkind", "the kind id the engine gave the test asset type", [this](const std::vector<std::string>&) {
            return Core::CommandResult{ m_AssetType != nullptr, std::to_string(int(m_AssetType->kind)) };
        });
        ctx.Log("loaded");
    }

    void OnInitialize(PluginContext&) override
    {
        m_Loads->ForceSet(m_Loads->Get() + 1);
    }

    bool OnPrepareReload(StateWriter& out) override
    {
        out.Set("frames", m_Frames);
        out.Set("pings", m_Pinger.calls);
        return true;
    }

    void OnRestore(StateReader& in) override
    {
        m_Frames = in.Get<int>("frames", -1);
        m_Pinger.calls = in.Get<int>("pings", -1);
        m_Counter->ForceSet(m_Frames);
    }

private:
    std::shared_ptr<Core::Resources::AssetTypeExtension> m_AssetType;
    Core::CVar<int>* m_Counter = nullptr;
    Core::CVar<int>* m_Loads = nullptr;
    int m_Frames = 0;
    Pinger m_Pinger;
};

SHARD_PLUGIN(TestPlugin)
