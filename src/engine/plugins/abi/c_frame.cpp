#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/abi/c_plugin.hpp"

// What the interfaces of the C ABI do once a frame, in a fixed order. The plugin manager calls these around the plugins' own on_frame_begin /
// on_frame_end.

namespace Shard::Engine::Plugins {

    void BeginCFrame(uint64_t frame)
    {
        CAbi::InputBeginFrame(frame);
        CAbi::RunMainPosts();
        CAbi::NetPollAll();
        CAbi::PhysicsBeginFrame();
    }

    void EndCFrame(uint64_t frame)
    {
        (void)frame;
        CAbi::DebugEndFrame();
    }
}
