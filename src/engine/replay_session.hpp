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

    /// @brief Plays back or records a run, and checks the state of the simulation along the way : what the apps that drive
    /// the frame loop (the headless runner, the player) call around every EngineInstance::Run.
    ///
    ///     ReplaySession session;
    ///     session.Play(replay);                                   // or session.Record(window, seed, step, world)
    ///     for (uint64_t frame = 0; frame < length; ++frame) {
    ///         session.BeforeFrame(frame, [&](const OSEvent& e) { window->PostEvent(e); });
    ///         engine.Run();
    ///         session.AfterFrame(engine);
    ///     }
    ///     session.Finish(engine);
    ///     if (session.Diverged()) { puts(session.Report().c_str()); return 1; }
    ///
    /// Playing : the events of each frame are handed to the callback, and every `check` of the replay is compared with the
    /// state hash of the run once that many frames have run. The first difference ends the comparison (Diverged) and the
    /// report says which part of the state differs.
    /// Recording : the events the window sends are stored, and a check is stored every CheckInterval frames and at the end.
    class ReplaySession {
    public:
        /// Plays `replay` back. With `verify` off its checks are not compared (the run is going to replace them : see --write-replay)
        void Play(Platform::ReplayData replay, bool verify = true);

        /// Records what `source` (a window) sends. `seed`, `fixedStep` and `world` are written in the header : what a replay needs
        /// to start the same run.
        void Record(Platform::OSEventSource& source, uint64_t seed, float fixedStep, const std::string& world);

        /// Recording : a check every this many frames (default 60). Playing : the state is printed to `onCheck` that often too.
        void SetCheckInterval(uint64_t frames) { m_Interval = frames; }

        /// Called with every state hash computed (a check of the replay, or an interval) : for the logs
        std::function<void(const SimulationStateHash&)> onState;

        void BeforeFrame(uint64_t frame, const std::function<void(const Platform::OSEvent&)>& post);
        void AfterFrame(IEngineContext& engine);
        /// After the last frame : a recording gets its final check, a playback its final state
        void Finish(IEngineContext& engine);

        bool Playing() const { return m_Mode == Mode::Play; }
        bool Recording() const { return m_Mode == Mode::Record; }

        /// Playing : a check did not match (what is left of the replay is not compared any more)
        bool Diverged() const { return m_Diverged; }
        const std::string& Report() const { return m_Report; }

        /// Checks of the replay that matched
        uint64_t ChecksPassed() const { return m_ChecksPassed; }
        /// The state after the last frame (valid once Finish ran)
        const SimulationStateHash& FinalState() const { return m_Final; }

        /// What was recorded, or what is being played
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
