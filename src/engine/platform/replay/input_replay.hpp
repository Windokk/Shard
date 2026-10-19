#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/platform/events/os_event.hpp"

namespace Shard::Engine::Core::Platform {

    /// @brief A recorded run : the input a player gave, frame by frame, and what the simulation looked like along the way.
    ///
    /// Frames count the calls of EngineInstance::Run, from 0. An event of frame F is delivered just before frame F runs (so the
    /// scripts of frame F see it). A check says "after `frames` frames have run, the state hash of the simulation is `hash`".
    /// With the same seed, the same fixed step and the same events, a deterministic run reaches every check : a replay is
    /// both an input script and a regression test.
    ///
    /// The file is text, one item per line, diff-able and writable by hand :
    ///
    ///     shard-replay 1
    ///     seed 12345
    ///     fixed-step #3c888889          (float, #bits in hex, or decimal when written by hand)
    ///     world determinism.world
    ///     frames 600
    ///     plugin game 0.0.0 0000000000000000          (a plugin that affects the simulation : id, version, hash of its content)
    ///     check 60 a1b2c3d4e5f60718 <transforms> <physics> <scripts>     (state hash after 60 frames, the three parts optional)
    ///     @12 key-down W
    ///     @12 mouse-move #43c80000 #42c80000 #40000000 #00000000
    ///     @30 key-up W
    ///
    /// Lines starting with # and blank lines are ignored. Keys and buttons go by name, so a replay survives the enum being
    /// reordered ; the floats go by bit pattern, so a replay reads back exactly what it recorded.
    struct ReplayData {
        static constexpr int kVersion = 1;

        struct TimedEvent {
            uint64_t frame = 0;
            OSEvent event;
        };

        struct Check {
            uint64_t frames = 0;
            uint64_t hash = 0;              // the whole state
            /// The parts the whole is made of (the transforms, the physics bodies, the scripts' fields) : when a run diverges,
            /// the one that differs says where to look. Optional in the file (a hand-written check has only the total).
            uint64_t parts[3] = {0, 0, 0};
            bool hasParts = false;
        };

        uint64_t seed = 0;
        float fixedStep = 0.0f;         // 0 : not recorded (the project's own step is used)
        std::string world;              // empty : the project's first world
        uint64_t frames = 0;            // how long the run lasted (0 : until the last event / check)
        std::vector<TimedEvent> events; // by frame, in the order they happened
        std::vector<Check> checks;      // by frames, ascending
        /// The plugins that change the simulation, as the run had them : "<id> <version> <content hash>" (the plugin manager's set).
        /// Empty : not recorded, nothing is checked. Written as `plugin` lines.
        std::vector<std::string> plugins;

        /// The text of the file
        std::string Serialize() const;

        /// @return false (and `error` says why, with the line) if the text is not a replay
        static bool Parse(std::string_view text, ReplayData& out, std::string& error);

        bool Save(const std::string& path, std::string& error) const;
        static bool Load(const std::string& path, ReplayData& out, std::string& error);

        /// The frame after the last thing the file mentions (an event, a check, `frames`) : how long to run when nothing says
        uint64_t Length() const;
    };

    /// @brief What a replay says about a key : the stable name used in files ("W", "LeftShift"). Empty for a value out of range.
    const char* KeyName(Input::Key key);
    bool KeyFromName(std::string_view name, Input::Key& out);

    /// @brief Records the input events of a window (or anything that is an OSEventSource) into a ReplayData.
    class ReplayRecorder {
    public:
        ~ReplayRecorder() { Detach(); }

        /// Starts listening. The events that follow are stamped with the frame given to SetFrame.
        void Attach(OSEventSource& source);
        void Detach();

        /// The frame that is about to run : call it before every EngineInstance::Run
        void SetFrame(uint64_t frame) { m_Frame = frame; }

        /// Only what changes the input state is kept : window moves, resizes, file drops and key repeats are not part of a replay
        static bool IsRecorded(const OSEvent& event);

        ReplayData& Data() { return m_Data; }
        const ReplayData& Data() const { return m_Data; }

    private:
        ReplayData m_Data;
        OSEventSource* m_Source = nullptr;
        OSEventSource::ListenerId m_Listener = 0;
        uint64_t m_Frame = 0;
    };

    /// @brief Feeds the events of a ReplayData to the engine, frame by frame
    class ReplayPlayer {
    public:
        explicit ReplayPlayer(ReplayData data) : m_Data(std::move(data)) {}

        /// Calls `post` for every event of `frame` (events of earlier frames that were not delivered yet go first)
        void Deliver(uint64_t frame, const std::function<void(const OSEvent&)>& post);

        /// The check that applies after `frames` frames have run, or null
        const ReplayData::Check* CheckAfter(uint64_t frames) const;

        const ReplayData& Data() const { return m_Data; }

    private:
        ReplayData m_Data;
        size_t m_Next = 0;
    };
}
