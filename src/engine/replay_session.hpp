#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "engine/platform/replay/input_replay.hpp"
#include "engine/simulation_state.hpp"

namespace Shard::Engine::Core {

    class IEngineContext;

    // Plays or records a run and checks the state hash along the way. Playing stops comparing at the first difference (Diverged) and reports which part differs.
    class ReplaySession {
    public:
        void Play(Platform::ReplayData replay, bool verify = true);

        void Record(Platform::OSEventSource& source, uint64_t seed, float fixedStep, const std::string& world);

        void SetCheckInterval(uint64_t frames) { m_Interval = frames; }

        std::function<void(const SimulationStateHash&)> onState;

        void BeforeFrame(uint64_t frame, const std::function<void(const Platform::OSEvent&)>& post);
        void AfterFrame(IEngineContext& engine);
        void Finish(IEngineContext& engine);

        bool Playing() const { return m_Mode == Mode::Play; }
        bool Recording() const { return m_Mode == Mode::Record; }

        bool Diverged() const { return m_Diverged; }
        const std::string& Report() const { return m_Report; }

        uint64_t ChecksPassed() const { return m_ChecksPassed; }
        const SimulationStateHash& FinalState() const { return m_Final; }

        const Platform::ReplayData& Data() const { return m_Mode == Mode::Play && m_Player ? m_Player->Data() : m_Recorder.Data(); }
        Platform::ReplayData TakeRecording();

    private:
        enum class Mode { None, Play, Record };

        static Platform::ReplayData::Check ToCheck(const SimulationStateHash& state);

        Mode m_Mode = Mode::None;
        bool m_Verify = true;
        uint64_t m_Interval = 60;
        std::unique_ptr<Platform::ReplayPlayer> m_Player;
        Platform::ReplayRecorder m_Recorder;
        bool m_Diverged = false;
        std::string m_Report;
        uint64_t m_ChecksPassed = 0;
        SimulationStateHash m_Final;
    };
}
