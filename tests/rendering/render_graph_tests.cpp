#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "engine/renderer/rhi/graph/render_graph.hpp"

using namespace Shard::Engine::Rendering;

namespace {

    GraphPassDesc Pass(const std::string& name, std::vector<PassAccess> accesses = {}, std::vector<std::string> after = {})
    {
        GraphPassDesc desc;
        desc.name = name;
        desc.accesses = std::move(accesses);
        desc.after = std::move(after);
        return desc;
    }

    GraphResourceDesc Texture(const std::string& name, uint32_t width, uint32_t height, uint32_t bytesPerTexel, bool imported = false)
    {
        GraphResourceDesc desc;
        desc.name = name;
        desc.width = width;
        desc.height = height;
        desc.bytesPerTexel = bytesPerTexel;
        desc.imported = imported;
        return desc;
    }

    std::vector<std::string> RunOrder(const CompiledGraph& graph)
    {
        std::vector<std::string> names;
        for (int index : graph.order)
            names.push_back(graph.passes[index].name);
        return names;
    }

    int PositionOf(const std::vector<std::string>& order, const std::string& name)
    {
        auto it = std::find(order.begin(), order.end(), name);
        return it == order.end() ? -1 : static_cast<int>(it - order.begin());
    }
}

TEST(RenderGraph, ReadersRunAfterTheProducerWhateverTheDeclarationOrder)
{
    RenderGraph graph;
    graph.DeclareResource(Texture("Screen", 64, 64, 4, true));

    // Declared backwards : the consumer first
    graph.AddPass(Pass("Present", {{"Lit", ResourceUsage::ShaderRead}, {"Screen", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("Lighting", {{"GBuffer", ResourceUsage::ShaderRead}, {"Lit", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("GeometryPass", {{"GBuffer", ResourceUsage::ColorTarget}}));

    CompiledGraph compiled = graph.Compile();
    ASSERT_TRUE(compiled.valid) << compiled.error;

    const std::vector<std::string> order = RunOrder(compiled);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], "GeometryPass");
    EXPECT_EQ(order[1], "Lighting");
    EXPECT_EQ(order[2], "Present");
}

TEST(RenderGraph, ProducersOfOneResourceKeepTheirDeclarationOrder)
{
    RenderGraph graph;
    graph.DeclareResource(Texture("Screen", 64, 64, 4, true));

    graph.AddPass(Pass("Scene", {{"Screen", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("Grid", {{"Screen", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("Outline", {{"Screen", ResourceUsage::ColorTarget}}));

    CompiledGraph compiled = graph.Compile();
    ASSERT_TRUE(compiled.valid);
    EXPECT_EQ(RunOrder(compiled), (std::vector<std::string>{"Scene", "Grid", "Outline"}));
}

TEST(RenderGraph, ExplicitDependenciesOrderPassesThatShareNothing)
{
    RenderGraph graph;
    graph.AddPass(Pass("B", {}, {"A"}));
    graph.AddPass(Pass("A", {}));

    CompiledGraph compiled = graph.Compile();
    ASSERT_TRUE(compiled.valid);
    EXPECT_EQ(RunOrder(compiled), (std::vector<std::string>{"A", "B"}));
}

TEST(RenderGraph, APassNobodyNeedsIsCulledWithWhatOnlyItFed)
{
    RenderGraph graph;
    graph.DeclareResource(Texture("Screen", 64, 64, 4, true));

    graph.AddPass(Pass("Useful", {{"Screen", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("Source", {{"Intermediate", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("Blur", {{"Intermediate", ResourceUsage::ShaderRead}, {"Blurred", ResourceUsage::ColorTarget}}));

    CompiledGraph compiled = graph.Compile();
    ASSERT_TRUE(compiled.valid);

    EXPECT_EQ(RunOrder(compiled), (std::vector<std::string>{"Useful"}));
    EXPECT_TRUE(compiled.Find("Source")->culled);
    EXPECT_TRUE(compiled.Find("Blur")->culled);
    EXPECT_FALSE(compiled.Find("Useful")->culled);
    EXPECT_EQ(compiled.CulledCount(), 2);
}

TEST(RenderGraph, AConsumerKeepsItsWholeChainAlive)
{
    RenderGraph graph;
    graph.DeclareResource(Texture("Screen", 64, 64, 4, true));

    graph.AddPass(Pass("Source", {{"A", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("Blur", {{"A", ResourceUsage::ShaderRead}, {"B", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("Compose", {{"B", ResourceUsage::ShaderRead}, {"Screen", ResourceUsage::ColorTarget}}));

    CompiledGraph compiled = graph.Compile();
    ASSERT_TRUE(compiled.valid);
    EXPECT_EQ(compiled.CulledCount(), 0);
    EXPECT_EQ(RunOrder(compiled), (std::vector<std::string>{"Source", "Blur", "Compose"}));
}

TEST(RenderGraph, ASideEffectAndAPassThatDeclaresNothingAreNeverCulled)
{
    RenderGraph graph;

    GraphPassDesc readback = Pass("Readback", {{"Unused", ResourceUsage::StorageWrite}});
    readback.sideEffects = true;
    graph.AddPass(readback);
    graph.AddPass(Pass("Legacy"));

    CompiledGraph compiled = graph.Compile();
    ASSERT_TRUE(compiled.valid);
    EXPECT_EQ(compiled.CulledCount(), 0);
    EXPECT_EQ(compiled.order.size(), 2u);
}

TEST(RenderGraph, ADisabledPassLeavesTheGraphAndWhatOnlyItFedGoesWithIt)
{
    RenderGraph graph;
    graph.DeclareResource(Texture("Screen", 64, 64, 4, true));

    graph.AddPass(Pass("Ao", {{"AoMap", ResourceUsage::ColorTarget}}));
    GraphPassDesc scene = Pass("Scene", {{"AoMap", ResourceUsage::ShaderRead}, {"Screen", ResourceUsage::ColorTarget}});
    graph.AddPass(scene);

    CompiledGraph enabled = graph.Compile();
    ASSERT_TRUE(enabled.valid);
    EXPECT_EQ(RunOrder(enabled), (std::vector<std::string>{"Ao", "Scene"}));

    GraphPassDesc ao = Pass("Ao", {{"AoMap", ResourceUsage::ColorTarget}});
    ao.enabled = false;
    graph.AddPass(ao);

    CompiledGraph disabled = graph.Compile();
    ASSERT_TRUE(disabled.valid);
    EXPECT_EQ(RunOrder(disabled), (std::vector<std::string>{"Scene"}));
    EXPECT_TRUE(disabled.Find("Ao")->disabled);
}

TEST(RenderGraph, ACycleIsReported)
{
    RenderGraph graph;
    graph.AddPass(Pass("A", {}, {"B"}));
    graph.AddPass(Pass("B", {}, {"A"}));

    CompiledGraph compiled = graph.Compile();
    EXPECT_FALSE(compiled.valid);
    EXPECT_NE(compiled.error.find("cycle"), std::string::npos);
    EXPECT_TRUE(compiled.order.empty());
}

TEST(RenderGraph, ReplacingAPassKeepsItsPlaceAndRemovingOneRenumbers)
{
    RenderGraph graph;
    graph.AddPass(Pass("A"));
    graph.AddPass(Pass("B"));
    graph.AddPass(Pass("C"));

    graph.AddPass(Pass("B", {{"X", ResourceUsage::ColorTarget}}));
    EXPECT_EQ(graph.PassCount(), 3u);
    EXPECT_EQ(graph.GetPasses()[1].name, "B");
    EXPECT_EQ(graph.GetPasses()[1].accesses.size(), 1u);

    graph.RemovePass("A");
    EXPECT_EQ(graph.PassCount(), 2u);
    EXPECT_FALSE(graph.HasPass("A"));
    EXPECT_TRUE(graph.HasPass("C"));
    graph.AddPass(Pass("C", {{"Y", ResourceUsage::ColorTarget}}));
    EXPECT_EQ(graph.PassCount(), 2u);
}

TEST(RenderGraph, ABarrierIsEmittedWhenTheUseOfAResourceChanges)
{
    RenderGraph graph;
    graph.DeclareResource(Texture("Screen", 64, 64, 4, true));

    graph.AddPass(Pass("Shadow", {{"Map", ResourceUsage::DepthTarget}}));
    graph.AddPass(Pass("Scene", {{"Map", ResourceUsage::ShaderRead}, {"Screen", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("Overlay", {{"Screen", ResourceUsage::ColorTarget}}));

    CompiledGraph compiled = graph.Compile();
    ASSERT_TRUE(compiled.valid);

    EXPECT_TRUE(compiled.Find("Shadow")->barriersBefore.empty());

    const CompiledPass* scene = compiled.Find("Scene");
    ASSERT_EQ(scene->barriersBefore.size(), 1u);
    EXPECT_EQ(scene->barriersBefore[0].resource, "Map");
    EXPECT_EQ(scene->barriersBefore[0].from, ResourceUsage::DepthTarget);
    EXPECT_EQ(scene->barriersBefore[0].to, ResourceUsage::ShaderRead);
    EXPECT_EQ(scene->memoryBarrierBefore & MemoryBarrierBit::TextureFetch, MemoryBarrierBit::TextureFetch);

    // Drawing into the same attachment twice needs nothing between the two
    EXPECT_TRUE(compiled.Find("Overlay")->barriersBefore.empty());
    EXPECT_EQ(compiled.Find("Overlay")->memoryBarrierBefore, MemoryBarrierBit::None);
}

TEST(RenderGraph, StorageWritesAreOrderedAfterThemselvesAndBeforeTheirReaders)
{
    RenderGraph graph;
    graph.AddPass(Pass("Build", {{"Grid", ResourceUsage::StorageWrite}}));
    graph.AddPass(Pass("Cull", {{"Grid", ResourceUsage::StorageWrite}}));
    GraphPassDesc draw = Pass("Draw", {{"Grid", ResourceUsage::StorageRead}});
    draw.sideEffects = true;
    graph.AddPass(draw);

    CompiledGraph compiled = graph.Compile();
    ASSERT_TRUE(compiled.valid);
    EXPECT_EQ(RunOrder(compiled), (std::vector<std::string>{"Build", "Cull", "Draw"}));

    // Write after write of a storage resource is a hazard : it gets its barrier
    ASSERT_EQ(compiled.Find("Cull")->barriersBefore.size(), 1u);
    EXPECT_EQ(compiled.Find("Cull")->barriersBefore[0].from, ResourceUsage::StorageWrite);
    EXPECT_EQ(compiled.Find("Cull")->barriersBefore[0].to, ResourceUsage::StorageWrite);
    EXPECT_NE(compiled.Find("Cull")->memoryBarrierBefore & MemoryBarrierBit::ShaderStorage, MemoryBarrierBit::None);
    EXPECT_NE(compiled.Find("Draw")->memoryBarrierBefore & MemoryBarrierBit::ShaderStorage, MemoryBarrierBit::None);
}

TEST(RenderGraph, ResourcesThatNeverLiveTogetherShareMemory)
{
    RenderGraph graph;
    graph.DeclareResource(Texture("Screen", 64, 64, 4, true));
    graph.DeclareResource(Texture("A", 100, 100, 4));        // 40000 bytes
    graph.DeclareResource(Texture("B", 100, 100, 4));
    graph.DeclareResource(Texture("C", 100, 100, 4));

    graph.AddPass(Pass("P1", {{"A", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("P2", {{"A", ResourceUsage::ShaderRead}, {"B", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("P3", {{"B", ResourceUsage::ShaderRead}, {"C", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("P4", {{"C", ResourceUsage::ShaderRead}, {"Screen", ResourceUsage::ColorTarget}}));

    CompiledGraph compiled = graph.Compile();
    ASSERT_TRUE(compiled.valid);

    // A lives over P1-P2, B over P2-P3, C over P3-P4 : A and C do not overlap, A and B (P2) and B and C (P3) do
    EXPECT_EQ(compiled.transientBytesWithoutAliasing, 3u * 40000u);
    EXPECT_EQ(compiled.aliasSlots.size(), 2u);
    EXPECT_EQ(compiled.transientBytesWithAliasing, 2u * 40000u);

    auto slotOf = [&](const std::string& name) {
        for (const ResourceLifetime& r : compiled.resources)
            if (r.name == name) return r.aliasSlot;
        return -2;
    };
    EXPECT_EQ(slotOf("A"), slotOf("C"));
    EXPECT_NE(slotOf("A"), slotOf("B"));
    EXPECT_EQ(slotOf("Screen"), -1);        // imported : never aliased
}

TEST(RenderGraph, ASlotGrowsToTheBiggestResourceThatUsesIt)
{
    RenderGraph graph;
    graph.DeclareResource(Texture("Screen", 64, 64, 4, true));
    graph.DeclareResource(Texture("Small", 10, 10, 4));
    graph.DeclareResource(Texture("Big", 100, 100, 4));

    graph.AddPass(Pass("P1", {{"Small", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("P2", {{"Small", ResourceUsage::ShaderRead}, {"Mid", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("P3", {{"Big", ResourceUsage::ColorTarget}, {"Mid", ResourceUsage::ShaderRead}}));
    graph.AddPass(Pass("P4", {{"Big", ResourceUsage::ShaderRead}, {"Screen", ResourceUsage::ColorTarget}}));

    CompiledGraph compiled = graph.Compile();
    ASSERT_TRUE(compiled.valid);

    ASSERT_FALSE(compiled.aliasSlots.empty());
    uint64_t largest = 0;
    for (const AliasSlot& slot : compiled.aliasSlots)
        largest = std::max(largest, slot.sizeBytes);
    EXPECT_EQ(largest, 40000u);
}

TEST(RenderGraph, TheListingAndTheGraphvizNameEveryPass)
{
    RenderGraph graph;
    graph.DeclareResource(Texture("Screen", 64, 64, 4, true));
    graph.AddPass(Pass("Scene", {{"Screen", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("Orphan", {{"Nothing", ResourceUsage::ColorTarget}}));

    CompiledGraph compiled = graph.Compile();
    ASSERT_TRUE(compiled.valid);

    const std::string text = RenderGraph::ToText(compiled, graph);
    EXPECT_NE(text.find("Scene"), std::string::npos);
    EXPECT_NE(text.find("culled : Orphan"), std::string::npos);

    const std::string dot = RenderGraph::ToDot(compiled, graph);
    EXPECT_NE(dot.find("digraph"), std::string::npos);
    EXPECT_NE(dot.find("p_Scene"), std::string::npos);
    EXPECT_NE(dot.find("p_Orphan"), std::string::npos);
}

TEST(RenderGraph, AnExplicitEdgeDecidesTheOrderOfTheProducersOfOneResource)
{
    RenderGraph graph;
    graph.DeclareResource(Texture("Scene", 64, 64, 4));
    graph.DeclareResource(Texture("Screen", 64, 64, 4, true));

    // Declared forward first, then lighting : but the forward pass says it comes after the lighting (what a deferred pipeline does)
    graph.AddPass(Pass("Forward", {{"Scene", ResourceUsage::ColorTarget}}, {"Lighting"}));
    graph.AddPass(Pass("Lighting", {{"Scene", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("Resolve", {{"Scene", ResourceUsage::ShaderRead}, {"Screen", ResourceUsage::ColorTarget}}));

    CompiledGraph compiled = graph.Compile();
    ASSERT_TRUE(compiled.valid) << compiled.error;
    EXPECT_EQ(RunOrder(compiled), (std::vector<std::string>{"Lighting", "Forward", "Resolve"}));
}

TEST(RenderGraph, APingPongBetweenTwoTargetsFollowsTheDependenciesOfItsPasses)
{
    RenderGraph graph;
    graph.DeclareResource(Texture("A", 64, 64, 16));
    graph.DeclareResource(Texture("B", 64, 64, 16));
    graph.DeclareResource(Texture("Screen", 64, 64, 4, true));

    // A jump flood : each pass reads what the one before it drew (which it names), and draws in the other target. The same two targets are
    // written several times, so what a pass reads is not the last write of the frame.
    graph.AddPass(Pass("Init", {{"A", ResourceUsage::ColorTarget}}));
    graph.AddPass(Pass("Step0", {{"B", ResourceUsage::ColorTarget}, {"A", ResourceUsage::ShaderRead}}, {"Init"}));
    graph.AddPass(Pass("Step1", {{"A", ResourceUsage::ColorTarget}, {"B", ResourceUsage::ShaderRead}}, {"Step0"}));
    graph.AddPass(Pass("Step2", {{"B", ResourceUsage::ColorTarget}, {"A", ResourceUsage::ShaderRead}}, {"Step1"}));
    graph.AddPass(Pass("Outline", {{"Screen", ResourceUsage::ColorTarget}, {"B", ResourceUsage::ShaderRead}}, {"Step2"}));

    CompiledGraph compiled = graph.Compile();
    ASSERT_TRUE(compiled.valid) << compiled.error;
    EXPECT_EQ(RunOrder(compiled), (std::vector<std::string>{"Init", "Step0", "Step1", "Step2", "Outline"}));
    EXPECT_EQ(compiled.CulledCount(), 0);
}
