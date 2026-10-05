#pragma once

#include <chrono>
#include <mutex>
#include <thread>
#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace Shard::Engine::Time{


    using Clock = std::chrono::steady_clock;

    struct TimeStamp{
        int nanoseconds;
        int milliseconds;
        int seconds;
        int minutes;
        int hours;
        int days;
        
        void Normalize() {
            if (nanoseconds >= 1'000'000) {
                milliseconds += nanoseconds / 1'000'000;
                nanoseconds %= 1'000'000;
            }

            if (milliseconds >= 1000) {
                seconds += milliseconds / 1000;
                milliseconds %= 1000;
            }

            if (seconds >= 60) {
                minutes += seconds / 60;
                seconds %= 60;
            }

            if (minutes >= 60) {
                hours += minutes / 60;
                minutes %= 60;
            }

            if (hours >= 24) {
                days += hours / 24;
                hours %= 24;
            }
        }
        
        std::string AsString() const {
            std::ostringstream oss;
            oss << days << "d "
                << std::setw(2) << std::setfill('0') << hours << "h:"
                << std::setw(2) << std::setfill('0') << minutes << "m:"
                << std::setw(2) << std::setfill('0') << seconds << "s:"
                << std::setw(3) << std::setfill('0') << milliseconds << "ms:"
                << std::setw(6) << std::setfill('0') << nanoseconds << "ns";
            return oss.str();
        }
    };
    
    class TimeManager {
        public:

            void Init(float fixedStep = 1.0f / 60.0f, float maxAccumulated = 0.25f){
                fixedDeltaTime = fixedStep;
                deltaTime = 0.0f;
                lastTime = Clock::now();
                appStartTime = lastTime;
                accumulator = 0;
                maxAccumulatedTime = maxAccumulated;
            }

            void Tick() {
                frameStartTime = Clock::now();
                auto delta = std::chrono::duration<float>(frameStartTime - lastTime);
                lastTime = frameStartTime;

                Advance(delta.count());
            }

            /// Feeds a frame of real time by hand instead of reading the clock. Kept separate from
            /// Tick() so the accumulation can be driven deterministically (tests, replays).
            void Advance(float realDeltaSeconds) {
                deltaTime = realDeltaSeconds;

                // The simulation clock only ever advances by whole fixed steps, so whatever real time
                // the frame took is banked here and paid out by ConsumeFixedSteps(). A frame that
                // stalls (breakpoint, level load, window drag) is clamped instead of banked in full,
                // otherwise the next frame would owe more simulation than it can run in time and every
                // following frame would owe even more - the classic spiral of death.
                float simulated = deltaTime * timeSpeed;
                if (simulated > maxAccumulatedTime)
                    simulated = maxAccumulatedTime;

                accumulator += simulated;
            }

            /// Number of fixed steps owed for this frame, removed from the accumulator as they are
            /// handed out. The caller is expected to run exactly that many simulation steps, each of
            /// GetFixedDeltaTime() seconds - that's what makes the simulation independent of framerate.
            int ConsumeFixedSteps() {
                if (fixedDeltaTime <= 0.0f)
                    return 0;

                int steps = static_cast<int>(accumulator / fixedDeltaTime);
                if (steps <= 0)
                    return 0;

                accumulator -= steps * fixedDeltaTime;
                return steps;
            }

            /// How far the frame sits between the last simulated step and the next one, in [0, 1).
            /// Useful to interpolate rendered transforms so motion stays smooth when the render rate
            /// isn't a multiple of the simulation rate.
            float GetFixedStepAlpha() const {
                return fixedDeltaTime > 0.0f ? accumulator / fixedDeltaTime : 0.0f;
            }

            /// Drops the banked time. Called when the simulation isn't running (editor, pause) so that
            /// resuming doesn't immediately replay everything that happened while it was stopped.
            void ResetAccumulator() { accumulator = 0.0f; }

            void SetFixedDeltaTime(float fixedStep) { fixedDeltaTime = fixedStep; }

            void SetMaxAccumulatedTime(float maxAccumulated) { maxAccumulatedTime = maxAccumulated; }

            float GetFixedDeltaTime() { return fixedDeltaTime; }
            float GetDeltaTime() { return deltaTime; }

            void SetTimeSpeed(float newSpeed = 1.0f) { timeSpeed = newSpeed; }
            float GetTimeSpeed() { return timeSpeed; }

            TimeStamp CurrentGlobalTime() {
                using namespace std::chrono;

                auto now = system_clock::now();
                auto now_time_t = system_clock::to_time_t(now);

                auto since_epoch = now.time_since_epoch();
                auto ms = duration_cast<milliseconds>(since_epoch) % 1000;
                auto ns = duration_cast<nanoseconds>(since_epoch) % 1'000'000;

                std::tm local_tm;
            #ifdef _WIN32
                localtime_s(&local_tm, &now_time_t);
            #else
                localtime_r(&now_time_t, &local_tm);
            #endif

                TimeStamp ts;
                ts.days = local_tm.tm_yday;
                ts.hours = local_tm.tm_hour;
                ts.minutes = local_tm.tm_min;
                ts.seconds = local_tm.tm_sec;
                ts.milliseconds = static_cast<int>(ms.count());
                ts.nanoseconds = static_cast<int>(ns.count());

                return ts;
            }

            TimeStamp CurrentAppTime() {
                using namespace std::chrono;

                auto now = Clock::now();
                auto elapsed = duration_cast<nanoseconds>(now - appStartTime);

                int64_t totalNs = elapsed.count();

                TimeStamp ts;

                const int64_t nsPerDay = 24LL * 60 * 60 * 1'000'000'000;
                const int64_t nsPerHour = 60LL * 60 * 1'000'000'000;
                const int64_t nsPerMinute = 60LL * 1'000'000'000;
                const int64_t nsPerSecond = 1'000'000'000;
                const int64_t nsPerMillisecond = 1'000'000;

                ts.days = static_cast<int>(totalNs / nsPerDay);
                totalNs %= nsPerDay;

                ts.hours = static_cast<int>(totalNs / nsPerHour);
                totalNs %= nsPerHour;

                ts.minutes = static_cast<int>(totalNs / nsPerMinute);
                totalNs %= nsPerMinute;

                ts.seconds = static_cast<int>(totalNs / nsPerSecond);
                totalNs %= nsPerSecond;

                ts.milliseconds = static_cast<int>(totalNs / nsPerMillisecond);
                ts.nanoseconds = static_cast<int>(totalNs % nsPerMillisecond);

                return ts;
            }
            
        private:

            float fixedDeltaTime;
            float deltaTime;

            float accumulator;
            float maxAccumulatedTime;

            Clock::time_point lastTime;
            Clock::time_point frameStartTime;

            Clock::time_point appStartTime;

            float timeSpeed = 1.0f;
    };
}