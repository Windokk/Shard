#pragma once

#include <memory>

namespace Shard::Engine::Objects{
    class Actor;
}

namespace Shard::Editor::Debug{

    struct CollisionDebugState{
        static CollisionDebugState& Get()
        {
            static CollisionDebugState state;
            return state;
        }

        std::weak_ptr<Engine::Objects::Actor> highlight;

        bool overlay = false;
        bool contacts = true;
        bool normals = true;
        bool events = true;
        float fadeSeconds = 1.5f;
    };
}
