#pragma once

#include <chrono>

#include "engine/platform/time/clock.hpp"
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

            void Tick(float forcedDelta = 0.0f) {
                frameStartTime = Clock::now();
                auto delta = std::chrono::duration<float>(frameStartTime - lastTime);
                lastTime = frameStartTime;

                Advance(forcedDelta > 0.0f ? forcedDelta : delta.count());
            }

            void Advance(float realDeltaSeconds) {
                deltaTime = realDeltaSeconds;

                // Only whole fixed steps are simulated, so the real time is banked. A stalled frame (breakpoint, world load) is clamped to avoid the spiral of death.
                float simulated = deltaTime * timeSpeed;
                if (simulated > maxAccumulatedTime)
                    simulated = maxAccumulatedTime;

                accumulator += simulated;
            }

            int ConsumeFixedSteps() {
                if (fixedDeltaTime <= 0.0f)
                    return 0;

                int steps = static_cast<int>(accumulator / fixedDeltaTime);
                if (steps <= 0)
                    return 0;

                accumulator -= steps * fixedDeltaTime;
                return steps;
            }

            float GetFixedStepAlpha() const {
                return fixedDeltaTime > 0.0f ? accumulator / fixedDeltaTime : 0.0f;
            }

            void ResetAccumulator() { accumulator = 0.0f; }

            void SetFixedDeltaTime(float fixedStep) { fixedDeltaTime = fixedStep; }

            void SetMaxAccumulatedTime(float maxAccumulated) { maxAccumulatedTime = maxAccumulated; }

            float GetFixedDeltaTime() { return fixedDeltaTime; }
            float GetDeltaTime() { return deltaTime; }

            void SetTimeSpeed(float newSpeed = 1.0f) { timeSpeed = newSpeed; }
            float GetTimeSpeed() { return timeSpeed; }

            TimeStamp CurrentGlobalTime() {
                const Core::Platform::LocalTime now = Core::Platform::GetLocalTime();

                TimeStamp ts;
                ts.days = now.dayOfYear;
                ts.hours = now.hour;
                ts.minutes = now.minute;
                ts.seconds = now.second;
                ts.milliseconds = now.millisecond;
                ts.nanoseconds = now.nanosecond;

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