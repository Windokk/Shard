#include "collision_types.hpp"

namespace Shard::Engine::Collision {

    const char* ToString(Response value)
    {
        switch (value) {
            case Response::Ignore: return "Ignore";
            case Response::Overlap: return "Overlap";
            case Response::Block: return "Block";
        }
        return "Ignore";
    }

    const char* ToString(Mode value)
    {
        switch (value) {
            case Mode::Solid: return "Solid";
            case Mode::Sensor: return "Sensor";
            case Mode::QueryOnly: return "QueryOnly";
            case Mode::Disabled: return "Disabled";
        }
        return "Solid";
    }

    const char* ToString(EventKind value)
    {
        switch (value) {
            case EventKind::OverlapBegin: return "OverlapBegin";
            case EventKind::OverlapEnd: return "OverlapEnd";
            case EventKind::Hit: return "Hit";
        }
        return "Hit";
    }

    const char* ToString(EndCause value)
    {
        switch (value) {
            case EndCause::None: return "None";
            case EndCause::Separated: return "Separated";
            case EndCause::Destroyed: return "Destroyed";
            case EndCause::Disabled: return "Disabled";
            case EndCause::ProfileChanged: return "ProfileChanged";
            case EndCause::Teleported: return "Teleported";
        }
        return "None";
    }

    bool TryParse(std::string_view text, Response& out)
    {
        for (Response r : { Response::Ignore, Response::Overlap, Response::Block })
            if (text == ToString(r)) { out = r; return true; }
        return false;
    }

    bool TryParse(std::string_view text, Mode& out)
    {
        for (Mode m : { Mode::Solid, Mode::Sensor, Mode::QueryOnly, Mode::Disabled })
            if (text == ToString(m)) { out = m; return true; }
        return false;
    }

    bool TryParse(std::string_view text, EventKind& out)
    {
        for (EventKind k : { EventKind::OverlapBegin, EventKind::OverlapEnd, EventKind::Hit })
            if (text == ToString(k)) { out = k; return true; }
        return false;
    }
}
