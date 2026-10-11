#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <chrono>
#include <thread>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/renderer/rhi/render_snapshot.hpp"

using namespace Shard::Engine::Rendering;

namespace {
    CameraProxy MakeCamera(float x)
    {
        CameraProxy camera;
        camera.valid = true;
        camera.position = glm::vec3(x, 0.0f, 0.0f);
        camera.view = glm::translate(glm::mat4(1.0f), -camera.position);
        camera.projection = glm::mat4(2.0f);
        camera.orthographic = true;
        return camera;
    }
}

TEST(RenderSnapshot, StartsEmpty) {
    RenderSnapshotBuffer buffer;
    EXPECT_EQ(buffer.Latest().frame, 0u);
    EXPECT_FALSE(buffer.Latest().camera.valid);
    EXPECT_EQ(buffer.PublishedCount(), 0u);

    RenderView view;
    EXPECT_FALSE(BuildMainView(buffer.Latest(), view));
}

TEST(RenderSnapshot, WrittenSnapshotIsInvisibleUntilPublished) {
    RenderSnapshotBuffer buffer;

    RenderSnapshot& next = buffer.BeginWrite();
    next.frame = 7;
    next.camera = MakeCamera(3.0f);

    EXPECT_EQ(buffer.Latest().frame, 0u);

    buffer.Publish();
    EXPECT_EQ(buffer.Latest().frame, 7u);
    EXPECT_EQ(buffer.PublishedCount(), 1u);
}

TEST(RenderSnapshot, BuildMainViewFollowsTheCamera) {
    RenderSnapshotBuffer buffer;
    RenderSnapshot& next = buffer.BeginWrite();
    next.camera = MakeCamera(5.0f);
    buffer.Publish();

    RenderView view;
    ASSERT_TRUE(BuildMainView(buffer.Latest(), view));
    EXPECT_EQ(view.position, glm::vec3(5.0f, 0.0f, 0.0f));
    EXPECT_EQ(view.view, buffer.Latest().camera.view);
    EXPECT_EQ(view.projection, buffer.Latest().camera.projection);
    EXPECT_TRUE(view.orthographic);
    EXPECT_EQ(view.lighting, ViewLighting::Scene);
}

TEST(RenderSnapshot, TwoBuffersAlternate) {
    RenderSnapshotBuffer buffer;

    const RenderSnapshot* first = nullptr;
    const RenderSnapshot* second = nullptr;

    RenderSnapshot& a = buffer.BeginWrite();
    a.frame = 1;
    first = &a;
    buffer.Publish();

    RenderSnapshot& b = buffer.BeginWrite();
    b.frame = 2;
    second = &b;
    buffer.Publish();

    RenderSnapshot& c = buffer.BeginWrite();
    EXPECT_NE(first, second);
    EXPECT_EQ(&c, first);                       // double buffered : the buffer of two publishes ago is reused
    EXPECT_EQ(buffer.Latest().frame, 2u);       // ...while the current one is untouched
}

TEST(RenderSnapshot, ReaderKeepsItsFrameWhileTheNextOneIsWritten) {
    RenderSnapshotBuffer buffer;

    {
        RenderSnapshot& next = buffer.BeginWrite();
        next.frame = 1;
        next.camera = MakeCamera(1.0f);
        buffer.Publish();
    }

    auto lease = buffer.Acquire();
    ASSERT_TRUE(lease);

    RenderSnapshot& next = buffer.BeginWrite();
    next.frame = 2;
    next.camera = MakeCamera(2.0f);
    buffer.Publish();

    EXPECT_EQ(lease->frame, 1u);
    EXPECT_EQ(lease->camera.position.x, 1.0f);
    EXPECT_EQ(buffer.Latest().frame, 2u);
}

TEST(RenderSnapshot, WriterWaitsForTheReaderOfTheBufferItReuses) {
    RenderSnapshotBuffer buffer;

    // frame 1 published into buffer B, then frame 2 into A : B is the one the next write reuses
    buffer.BeginWrite().frame = 1;
    buffer.Publish();
    auto lease = std::make_unique<RenderSnapshotBuffer::ReadLease>(buffer.Acquire());
    buffer.BeginWrite().frame = 2;
    buffer.Publish();

    // The lease holds the buffer of frame 1 : the third write has to wait for its release
    std::atomic<bool> writing{false};
    std::atomic<bool> wrote{false};
    std::thread writer([&] {
        writing = true;
        buffer.BeginWrite().frame = 3;
        buffer.Publish();
        wrote = true;
    });

    while (!writing)
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(wrote);
    EXPECT_EQ((*lease)->frame, 1u);

    lease.reset();
    writer.join();
    EXPECT_TRUE(wrote);
    EXPECT_EQ(buffer.Latest().frame, 3u);
}

TEST(RenderSnapshot, ResetKeepsMemory) {
    RenderSnapshot snapshot;
    snapshot.meshes.resize(100);
    snapshot.lights.resize(10);
    const size_t capacity = snapshot.meshes.capacity();

    snapshot.Reset();
    EXPECT_TRUE(snapshot.meshes.empty());
    EXPECT_TRUE(snapshot.lights.empty());
    EXPECT_GE(snapshot.meshes.capacity(), capacity);
}

TEST(RenderSnapshot, FindByKey) {
    std::vector<LightProxy> lights(3);
    lights[0].key = 4;
    lights[1].key = 9;
    lights[2].key = 12;

    const LightProxy* found = RenderSnapshot::Find(lights, 9);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found, &lights[1]);
    EXPECT_EQ(RenderSnapshot::Find(lights, 10), nullptr);
}

TEST(RenderSnapshot, CameraAspectRatio) {
    CameraProxy camera;
    EXPECT_FLOAT_EQ(camera.AspectRatio(), 1.0f);        // no size yet : square rather than a division by zero
    camera.width = 1920;
    camera.height = 1080;
    EXPECT_FLOAT_EQ(camera.AspectRatio(), 1920.0f / 1080.0f);
}

TEST(RenderSnapshot, ProbeGridFillsTheVolumeInsetByHalfACell) {
    ProbeVolumeProxy proxy;
    proxy.volume.center = glm::vec3(10.0f, 0.0f, 0.0f);
    proxy.volume.halfExtent = glm::vec3(4.0f, 2.0f, 2.0f);
    proxy.probeCounts = glm::ivec3(4, 2, 2);

    EXPECT_EQ(proxy.GridSpacing(), glm::vec3(2.0f, 2.0f, 2.0f));
    // The box starts at (6, -2, -2) : the first probe sits half a cell inside it
    EXPECT_EQ(proxy.GridOrigin(), glm::vec3(7.0f, -1.0f, -1.0f));
    // ... and so does the last one, on the other side
    EXPECT_EQ(proxy.GridOrigin() + proxy.GridSpacing() * glm::vec3(3.0f, 1.0f, 1.0f), glm::vec3(13.0f, 1.0f, 1.0f));
}

TEST(RenderSnapshot, ProbeGridToleratesAZeroCount) {
    ProbeVolumeProxy proxy;
    proxy.probeCounts = glm::ivec3(0, 0, 0);
    const glm::vec3 spacing = proxy.GridSpacing();
    EXPECT_TRUE(std::isfinite(spacing.x) && std::isfinite(spacing.y) && std::isfinite(spacing.z));
}
