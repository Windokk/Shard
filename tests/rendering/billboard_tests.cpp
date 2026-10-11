#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/renderer/features/billboards/billboard_atlas.hpp"
#include "engine/renderer/features/billboards/billboard_renderer.hpp"
#include "engine/renderer/rhi/render_snapshot.hpp"

using namespace Shard::Engine::Rendering;

namespace {
    // A camera at `position` looking down -Z, 1280x720
    RenderSnapshot MakeSnapshot(glm::vec3 position = glm::vec3(0.0f))
    {
        RenderSnapshot snapshot;
        snapshot.viewportWidth = 1280;
        snapshot.viewportHeight = 720;
        snapshot.camera.valid = true;
        snapshot.camera.position = position;
        snapshot.camera.view = glm::lookAt(position, position + glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
        snapshot.camera.projection = glm::perspective(glm::radians(60.0f), 1280.0f / 720.0f, 0.1f, 1000.0f);
        snapshot.camera.viewProjection = snapshot.camera.projection * snapshot.camera.view;
        return snapshot;
    }

    BillboardProxy MakeProxy(uint64_t key, glm::dvec3 position, float size = 40.0f, uint8_t flags = BillboardFlag::ScreenSpaceSize | BillboardFlag::DepthTest | BillboardFlag::Pickable)
    {
        BillboardProxy proxy;
        proxy.key = key;
        proxy.objectId = int(key);
        proxy.position = position;
        proxy.size = size;
        proxy.flags = flags;
        return proxy;
    }
}

TEST(BillboardAtlasTest, TheSheetIs1024With64PixelTilesFor256Sprites) {
    EXPECT_EQ(BillboardAtlas::kSheetSize, 1024u);
    EXPECT_EQ(BillboardAtlas::kTileSize, 64u);
    EXPECT_EQ(BillboardAtlas::kMaxSprites, 256u);
}

TEST(BillboardAtlasTest, ASheetIsResampledToOneTilePerFrame) {
    BillboardAtlas& atlas = BillboardAtlas::Get();
    const uint32_t before = atlas.GetSpriteCount();

    std::vector<uint8_t> big(size_t(64) * 32 * 4, 255);     // two 32 x 32 frames
    EXPECT_EQ(atlas.RegisterSheet("tests/big.png", 2, 64, 32, big.data()), before);
    EXPECT_EQ(atlas.GetSpriteCount(), before + 2);
}

TEST(BillboardAtlasTest, AnUnknownNameIsTheGenericIcon) {
    BillboardAtlas& atlas = BillboardAtlas::Get();
    EXPECT_EQ(atlas.Find("no_such_icon"), atlas.GetBuiltin(BuiltinSprite::Generic));
    EXPECT_EQ(atlas.LoadFolder("no/such/folder"), 0u);
}

TEST(BillboardAtlasTest, BuiltInIconsHaveAGlyphAndATransparentBackground) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(BuiltinSprite::Count); i++) {
        const std::vector<uint8_t> pixels = BillboardAtlas::MakeBuiltinPixels(static_cast<BuiltinSprite>(i), BillboardAtlas::kBuiltinSize);
        const uint32_t size = BillboardAtlas::kBuiltinSize;
        ASSERT_EQ(pixels.size(), size_t(size) * size * 4);

        EXPECT_EQ(pixels[3], 0) << "icon " << i << " corner should be empty";

        size_t opaque = 0;
        for (size_t p = 3; p < pixels.size(); p += 4)
            opaque += pixels[p] == 255;
        EXPECT_GT(opaque, 8u) << "icon " << i << " has no glyph";
    }
}

TEST(BillboardAtlasTest, ASheetIsSplitInFramesAndRegisteredOnce) {
    BillboardAtlas& atlas = BillboardAtlas::Get();
    const uint32_t before = atlas.GetSpriteCount();

    std::vector<uint8_t> sheet(size_t(8) * 4 * 4, 0);
    for (size_t i = 0; i < sheet.size(); i += 4) sheet[i] = 200;

    const uint32_t first = atlas.RegisterSheet("tests/sheet_a.png", 2, 8, 4, sheet.data());
    EXPECT_EQ(first, before);
    EXPECT_EQ(atlas.GetSpriteCount(), before + 2);

    // The same key is the same sprites
    EXPECT_EQ(atlas.RegisterSheet("tests/sheet_a.png", 2, 8, 4, sheet.data()), first);
    EXPECT_EQ(atlas.GetSpriteCount(), before + 2);

    // The same image cut differently is another sheet
    EXPECT_NE(atlas.RegisterSheet("tests/sheet_a.png", 4, 8, 4, sheet.data()), first);
}

TEST(BillboardAtlasTest, ABadSheetFallsBackToTheGenericIcon) {
    BillboardAtlas& atlas = BillboardAtlas::Get();
    const uint32_t generic = atlas.GetBuiltin(BuiltinSprite::Generic);
    const uint32_t before = atlas.GetSpriteCount();

    EXPECT_EQ(atlas.RegisterSheet("tests/bad", 0, 8, 4, nullptr), generic);
    EXPECT_EQ(atlas.RegisterSheet("tests/bad2", 16, 8, 4, nullptr), generic);
    EXPECT_EQ(atlas.GetSpriteCount(), before);
}

TEST(BillboardInstancesTest, NothingWithoutACameraOrBillboards) {
    std::vector<BillboardInstance> out;
    uint32_t depthTested = 99;

    RenderSnapshot snapshot = MakeSnapshot();
    BillboardRenderer::BuildInstances(snapshot, true, out, depthTested);
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(depthTested, 0u);

    snapshot.billboards.push_back(MakeProxy(1, {0, 0, -5}));
    snapshot.camera.valid = false;
    BillboardRenderer::BuildInstances(snapshot, true, out, depthTested);
    EXPECT_TRUE(out.empty());
}

TEST(BillboardInstancesTest, EditorOnlyBillboardsFollowTheEditorSwitch) {
    RenderSnapshot snapshot = MakeSnapshot();
    snapshot.billboards.push_back(MakeProxy(1, {0, 0, -5}, 40.0f, BillboardFlag::ScreenSpaceSize | BillboardFlag::DepthTest | BillboardFlag::EditorOnly));
    snapshot.billboards.push_back(MakeProxy(2, {1, 0, -5}));

    std::vector<BillboardInstance> out;
    uint32_t depthTested = 0;

    BillboardRenderer::BuildInstances(snapshot, false, out, depthTested);
    EXPECT_EQ(out.size(), 1u);

    BillboardRenderer::BuildInstances(snapshot, true, out, depthTested);
    EXPECT_EQ(out.size(), 2u);
}

TEST(BillboardInstancesTest, ASizeOfZeroDrawsNothing) {
    RenderSnapshot snapshot = MakeSnapshot();
    snapshot.billboards.push_back(MakeProxy(1, {0, 0, -5}, 0.0f));

    std::vector<BillboardInstance> out;
    uint32_t depthTested = 0;
    BillboardRenderer::BuildInstances(snapshot, true, out, depthTested);
    EXPECT_TRUE(out.empty());
}

TEST(BillboardInstancesTest, PositionsComeRelativeToTheCameraWithoutLosingPrecision) {
    // Far from the origin a float cannot hold a centimeter ; the difference of two 64-bit positions can
    RenderSnapshot snapshot = MakeSnapshot(glm::vec3(100000.0f, 0.0f, 0.0f));
    snapshot.billboards.push_back(MakeProxy(1, {100000.0 + 0.125, 2.0, -5.0}));

    std::vector<BillboardInstance> out;
    uint32_t depthTested = 0;
    BillboardRenderer::BuildInstances(snapshot, true, out, depthTested);

    ASSERT_EQ(out.size(), 1u);
    EXPECT_FLOAT_EQ(out[0].positionSize.x, 0.125f);
    EXPECT_FLOAT_EQ(out[0].positionSize.y, 2.0f);
    EXPECT_FLOAT_EQ(out[0].positionSize.z, -5.0f);
    EXPECT_FLOAT_EQ(out[0].positionSize.w, 40.0f);
}

TEST(BillboardInstancesTest, TheDepthTestedLayerComesFirstEachLayerFarthestFirst) {
    RenderSnapshot snapshot = MakeSnapshot();
    const uint8_t over = BillboardFlag::ScreenSpaceSize;
    snapshot.billboards.push_back(MakeProxy(1, {0, 0, -2}));
    snapshot.billboards.push_back(MakeProxy(2, {0, 0, -9}, 40.0f, over));
    snapshot.billboards.push_back(MakeProxy(3, {0, 0, -8}));
    snapshot.billboards.push_back(MakeProxy(4, {0, 0, -3}, 40.0f, over));

    std::vector<BillboardInstance> out;
    uint32_t depthTested = 0;
    BillboardRenderer::BuildInstances(snapshot, true, out, depthTested);

    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(depthTested, 2u);
    EXPECT_FLOAT_EQ(out[0].positionSize.z, -8.0f);
    EXPECT_FLOAT_EQ(out[1].positionSize.z, -2.0f);
    EXPECT_FLOAT_EQ(out[2].positionSize.z, -9.0f);
    EXPECT_FLOAT_EQ(out[3].positionSize.z, -3.0f);
}

TEST(BillboardInstancesTest, FacingAndFlagsReachTheShader) {
    RenderSnapshot snapshot = MakeSnapshot();
    BillboardProxy proxy = MakeProxy(1, {0, 0, -4});
    proxy.facing = BillboardProxyFacing::AxisY;
    proxy.tint = glm::vec4(0.5f, 0.25f, 1.0f, 0.75f);
    snapshot.billboards.push_back(proxy);

    std::vector<BillboardInstance> out;
    uint32_t depthTested = 0;
    BillboardRenderer::BuildInstances(snapshot, true, out, depthTested);

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].params.x, 1u);
    EXPECT_EQ(out[0].params.y & BillboardFlag::ScreenSpaceSize, uint32_t(BillboardFlag::ScreenSpaceSize));
    EXPECT_EQ(out[0].tint, proxy.tint);
}

TEST(BillboardPickTest, AClickOnTheIconSelectsItsActor) {
    RenderSnapshot snapshot = MakeSnapshot();
    snapshot.billboards.push_back(MakeProxy(7, {0, 0, -5}));      // dead centre : pixel (640, 360)

    const BillboardPick hit = BillboardRenderer::Pick(snapshot, true, 650.0f, 350.0f, 1280.0f, 720.0f);
    EXPECT_TRUE(hit.hit);
    EXPECT_EQ(hit.objectId, 7);
    EXPECT_NEAR(hit.distance, 5.0f, 1e-3f);

    EXPECT_FALSE(BillboardRenderer::Pick(snapshot, true, 800.0f, 360.0f, 1280.0f, 720.0f).hit);
}

TEST(BillboardPickTest, TheNearestIconWinsWhereTheyOverlap) {
    RenderSnapshot snapshot = MakeSnapshot();
    snapshot.billboards.push_back(MakeProxy(1, {0, 0, -9}));
    snapshot.billboards.push_back(MakeProxy(2, {0, 0, -4}));
    snapshot.billboards.push_back(MakeProxy(3, {0, 0, -6}));

    const BillboardPick hit = BillboardRenderer::Pick(snapshot, true, 640.0f, 360.0f, 1280.0f, 720.0f);
    EXPECT_TRUE(hit.hit);
    EXPECT_EQ(hit.objectId, 2);
}

TEST(BillboardPickTest, NotPickableEditorOnlyOrBehindTheCameraIsIgnored) {
    RenderSnapshot snapshot = MakeSnapshot();
    snapshot.billboards.push_back(MakeProxy(1, {0, 0, -5}, 40.0f, BillboardFlag::ScreenSpaceSize | BillboardFlag::DepthTest));   // not pickable
    snapshot.billboards.push_back(MakeProxy(2, {0, 0, -5}, 40.0f, BillboardFlag::ScreenSpaceSize | BillboardFlag::Pickable | BillboardFlag::EditorOnly));
    snapshot.billboards.push_back(MakeProxy(3, {0, 0, 5}));                                                                       // behind

    EXPECT_FALSE(BillboardRenderer::Pick(snapshot, false, 640.0f, 360.0f, 1280.0f, 720.0f).hit);
    EXPECT_EQ(BillboardRenderer::Pick(snapshot, true, 640.0f, 360.0f, 1280.0f, 720.0f).objectId, 2);
}

TEST(BillboardPickTest, AWorldSizedBillboardShrinksWithDistance) {
    RenderSnapshot snapshot = MakeSnapshot();
    // 2 meters wide, flags without ScreenSpaceSize
    snapshot.billboards.push_back(MakeProxy(1, {0, 0, -4}, 2.0f, BillboardFlag::Pickable));

    // Near : 1 meter at 4 meters is ~ 1/(4*tan(30deg)) of the half height = ~ 156 pixels
    EXPECT_TRUE(BillboardRenderer::Pick(snapshot, true, 640.0f + 120.0f, 360.0f, 1280.0f, 720.0f).hit);
    EXPECT_FALSE(BillboardRenderer::Pick(snapshot, true, 640.0f + 200.0f, 360.0f, 1280.0f, 720.0f).hit);
}
