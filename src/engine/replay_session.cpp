#include "engine/replay_session.hpp"

#include <memory>

#include "engine/core/determinism/state_hash.hpp"
#include "engine/world/engine.hpp"

namespace Shard::Engine::Core {

    using Platform::ReplayData;

    ReplayData::Check ReplaySession::ToCheck(const SimulationStateHash& state)
    {
        ReplayData::Check check;
        check.frames = state.frame;
        check.hash = state.total;
        check.parts[0] = state.transforms;
        check.parts[1] = state.physics;
        check.parts[2] = state.scripts;
        check.hasParts = true;
        return check;
    }

    void ReplaySession::Play(ReplayData replay, bool verify)
    {
        m_Mode = Mode::Play;
        m_Verify = verify;
        m_Recorder.Detach();
        m_Player = std::make_unique<Platform::ReplayPlayer>(std::move(replay));
        m_Diverged = false;
        m_Report.clear();
        m_ChecksPassed = 0;
    }

    void ReplaySession::Record(Platform::OSEventSource& source, uint64_t seed, float fixedStep, const std::string& world)
    {
        m_Mode = Mode::Record;
        m_Player.reset();
        m_Recorder.Data() = ReplayData{};
        m_Recorder.Data().seed = seed;
        m_Recorder.Data().fixedStep = fixedStep;
        m_Recorder.Data().world = world;
        m_Recorder.Attach(source);
    }

    void ReplaySession::BeforeFrame(uint64_t frame, const std::function<void(const Platform::OSEvent&)>& post)
    {
        if (m_Mode == Mode::Play && m_Player)
            m_Player->Deliver(frame, post);
        else if (m_Mode == Mode::Record)
            m_Recorder.SetFrame(frame);
    }

    void ReplaySession::AfterFrame(IEngineContext& engine)
    {
        const uint64_t frames = engine.GetFrameNumber();

        if (m_Mode == Mode::Record)
        {
            if (m_Interval > 0 && frames % m_Interval == 0)
            {
                const SimulationStateHash state = HashSimulationState(engine);
                m_Recorder.Data().checks.push_back(ToCheck(state));
                if (onState)
                    onState(state);
            }
            return;
        }

        if (m_Mode != Mode::Play || !m_Player)
            return;

        const ReplayData::Check* check = (m_Diverged || !m_Verify) ? nullptr : m_Player->CheckAfter(frames);
        const bool print = m_Interval > 0 && frames % m_Interval == 0;
        if (!check && !print)
            return;

        const SimulationStateHash state = HashSimulationState(engine);
        if (onState)
            onState(state);

        if (!check)
            return;

        if (check->hash == state.total)
        {
            ++m_ChecksPassed;
            return;
        }

        m_Diverged = true;
        m_Report = "the run diverged from the replay after " + std::to_string(frames) + " frames : expected state " +
                   Determinism::HashToString(check->hash) + ", got " + Determinism::HashToString(state.total);
        if (check->hasParts)
        {
            const char* names[3] = {"transforms", "physics", "scripts"};
            const uint64_t got[3] = {state.transforms, state.physics, state.scripts};
            std::string differing;
            for (int i = 0; i < 3; ++i)
                if (check->parts[i] != got[i])
                    differing += (differing.empty() ? "" : ", ") + std::string(names[i]);
            m_Report += differing.empty() ? " (every part matches : the physics step count differs)" : " (differs : " + differing + ")";
        }
    }

    void ReplaySession::Finish(IEngineContext& engine)
    {
        m_Final = HashSimulationState(engine);

        if (m_Mode == Mode::Record)
        {
            auto& checks = m_Recorder.Data().checks;
            if (checks.empty() || checks.back().frames != m_Final.frame)
                checks.push_back(ToCheck(m_Final));
            m_Recorder.Data().frames = m_Final.frame;
            m_Recorder.Detach();
        }
        else if (m_Mode == Mode::Play && m_Player && m_Verify && !m_Diverged)
        {
            // A check set after the last frame that ran can never be reached : the run is shorter than the replay says
            for (const ReplayData::Check& check : m_Player->Data().checks)
            {
                if (check.frames > m_Final.frame)
                {
                    m_Diverged = true;
                    m_Report = "the run ended after " + std::to_string(m_Final.frame) + " frames, before the check of frame " + std::to_string(check.frames);
                    break;
                }
            }
        }
    }

    ReplayData ReplaySession::TakeRecording()
    {
        return std::move(m_Recorder.Data());
    }
}
