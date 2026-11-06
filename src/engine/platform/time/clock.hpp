#pragma once

#include <cstdint>

namespace Shard::Engine::Core::Platform {

    uint64_t NowNanoseconds();

    inline double NowSeconds() { return static_cast<double>(NowNanoseconds()) * 1e-9; }

    void PreciseSleep(double seconds);

    struct LocalTime {
        int year = 0;
        int month = 0;
        int day = 0;
        int dayOfYear = 0;
        int hour = 0;
        int minute = 0;
        int second = 0;
        int millisecond = 0;
        int nanosecond = 0;
    };

    LocalTime GetLocalTime();
}
