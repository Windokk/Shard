#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/abi/c_plugin.hpp"

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
