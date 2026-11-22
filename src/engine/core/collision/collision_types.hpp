#pragma once

#include <cstdint>
#include <string_view>

namespace Shard::Engine::Collision {

    enum class Response : uint8_t { Ignore = 0, Overlap = 1, Block = 2 };
    enum class Mode : uint8_t { Solid = 0, Sensor = 1, QueryOnly = 2, Disabled = 3 };
    enum class EventKind : uint8_t { OverlapBegin = 0, OverlapEnd = 1, Hit = 2 };
    enum class EndCause : uint8_t { None = 0, Separated = 1, Destroyed = 2, Disabled = 3, ProfileChanged = 4, Teleported = 5 };

    inline Response Weakest(Response a, Response b) { return a < b ? a : b; }
    inline Response Strongest(Response a, Response b) { return a < b ? b : a; }

    const char* ToString(Response value);
    const char* ToString(Mode value);
    const char* ToString(EventKind value);
    const char* ToString(EndCause value);

    bool TryParse(std::string_view text, Response& out);
    bool TryParse(std::string_view text, Mode& out);
    bool TryParse(std::string_view text, EventKind& out);
}
