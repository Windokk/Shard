#include "engine/platform/time/clock.hpp"

#include "engine/platform/atomic/atomic.hpp"

#include <chrono>
#include <ctime>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <time.h>
#endif

namespace Shard::Engine::Core::Platform {

    uint64_t NowNanoseconds()
    {
    #if defined(_WIN32)
        static const LARGE_INTEGER frequency = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        // split to not overflow 64 bits : counter * 1e9 overflows after a few hours of uptime
        const uint64_t seconds = static_cast<uint64_t>(counter.QuadPart / frequency.QuadPart);
        const uint64_t rest = static_cast<uint64_t>(counter.QuadPart % frequency.QuadPart);
        return seconds * 1'000'000'000ull + rest * 1'000'000'000ull / static_cast<uint64_t>(frequency.QuadPart);
    #else
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull + static_cast<uint64_t>(ts.tv_nsec);
    #endif
    }

    void PreciseSleep(double seconds)
    {
        if (seconds <= 0.0)
            return;

        const uint64_t start = NowNanoseconds();
        const uint64_t target = start + static_cast<uint64_t>(seconds * 1e9);

        // The OS sleeps wake up late by up to a scheduler tick : only trust it for what is left over a margin.
        constexpr double kSpinMargin = 0.002;
        const double sleepSeconds = seconds - kSpinMargin;

        if (sleepSeconds > 0.0)
        {
        #if defined(_WIN32)
            // A waitable timer with CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (Windows 10 1803+) wakes up within ~0.5 ms
            static thread_local HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
            if (timer)
            {
                LARGE_INTEGER due;
                due.QuadPart = -static_cast<LONGLONG>(sleepSeconds * 1e7);  // 100 ns units, negative = relative
                if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
                    WaitForSingleObject(timer, INFINITE);
            }
            else
                Sleep(static_cast<DWORD>(sleepSeconds * 1000.0));
        #else
            timespec req;
            req.tv_sec = static_cast<time_t>(sleepSeconds);
            req.tv_nsec = static_cast<long>((sleepSeconds - static_cast<double>(req.tv_sec)) * 1e9);
            nanosleep(&req, nullptr);
        #endif
        }

        while (NowNanoseconds() < target)
        {
            CpuRelax();
        }
    }

    LocalTime GetLocalTime()
    {
        using namespace std::chrono;

        const auto now = system_clock::now();
        const std::time_t timeT = system_clock::to_time_t(now);

        std::tm tm{};
    #if defined(_WIN32)
        localtime_s(&tm, &timeT);
    #else
        localtime_r(&timeT, &tm);
    #endif

        const auto sinceEpoch = now.time_since_epoch();
        const auto ms = duration_cast<milliseconds>(sinceEpoch) % 1000;
        const auto ns = duration_cast<nanoseconds>(sinceEpoch) % 1'000'000;

        LocalTime t;
        t.year = tm.tm_year + 1900;
        t.month = tm.tm_mon + 1;
        t.day = tm.tm_mday;
        t.dayOfYear = tm.tm_yday;
        t.hour = tm.tm_hour;
        t.minute = tm.tm_min;
        t.second = tm.tm_sec;
        t.millisecond = static_cast<int>(ms.count());
        t.nanosecond = static_cast<int>(ns.count());
        return t;
    }
}
