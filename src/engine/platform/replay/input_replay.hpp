#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/platform/events/os_event.hpp"

namespace Shard::Engine::Core::Platform {

    // Replay file: text, one item per line.
    // Header (seed, fixed-step, world, frames, plugin), `check <frames> <hash>`, then `@frame key-down W`. Floats as #bits hex.
    struct ReplayData {
        static constexpr int kVersion = 1;

        struct TimedEvent {
            uint64_t frame = 0;
            OSEvent event;
        };

        struct Check {
            uint64_t frames = 0;
            uint64_t hash = 0;
            // Transforms, physics bodies, script fields. A diverging run shows which part differs. Optional in the file.
            uint64_t parts[3] = {0, 0, 0};
            bool hasParts = false;
        };

        uint64_t seed = 0;
        float fixedStep = 0.0f;
        std::string world;
        uint64_t frames = 0;
        std::vector<TimedEvent> events;
        std::vector<Check> checks;
        std::vector<std::string> plugins;

        std::string Serialize() const;

        static bool Parse(std::string_view text, ReplayData& out, std::string& error);

        bool Save(const std::string& path, std::string& error) const;
        static bool Load(const std::string& path, ReplayData& out, std::string& error);

        uint64_t Length() const;
    };

    const char* KeyName(Input::Key key);
    bool KeyFromName(std::string_view name, Input::Key& out);

    class ReplayRecorder {
    public:
        ~ReplayRecorder() { Detach(); }

        void Attach(OSEventSource& source);
        void Detach();

        void SetFrame(uint64_t frame) { m_Frame = frame; }

        static bool IsRecorded(const OSEvent& event);

        ReplayData& Data() { return m_Data; }
        const ReplayData& Data() const { return m_Data; }

    private:
        ReplayData m_Data;
        OSEventSource* m_Source = nullptr;
        OSEventSource::ListenerId m_Listener = 0;
        uint64_t m_Frame = 0;
    };

    class ReplayPlayer {
    public:
        explicit ReplayPlayer(ReplayData data) : m_Data(std::move(data)) {}

        void Deliver(uint64_t frame, const std::function<void(const OSEvent&)>& post);

        const ReplayData::Check* CheckAfter(uint64_t frames) const;

        const ReplayData& Data() const { return m_Data; }

    private:
        ReplayData m_Data;
        size_t m_Next = 0;
    };
}
