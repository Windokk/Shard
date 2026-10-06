#pragma once

#include <cstdint>

namespace Shard::Engine::Core::Platform {

    /// @brief Monotonic high resolution clock, in nanoseconds since an arbitrary point (boot, usually).
    /// QueryPerformanceCounter / clock_gettime(CLOCK_MONOTONIC).
    uint64_t NowNanoseconds();

    inline double NowSeconds() { return static_cast<double>(NowNanoseconds()) * 1e-9; }

    /// @brief Sleeps for about `seconds` with a precision of well under a millisecond : an OS sleep for the bulk of the
    /// time (high resolution timer on Windows, where Sleep() is only accurate to the scheduler tick), then a spin on
    /// the clock for the rest. Used to pace frames.
    void PreciseSleep(double seconds);

    /// @brief Calendar time in the local time zone.
    struct LocalTime {
        int year = 0;
        int month = 0;          // 1-12
        int day = 0;            // 1-31
        int dayOfYear = 0;      // 0-365
        int hour = 0;
        int minute = 0;
        int second = 0;
        int millisecond = 0;
        int nanosecond = 0;     // within the millisecond (0 - 999 999)
    };

    LocalTime GetLocalTime();
}
