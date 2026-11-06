// Shard C++ SDK : the simulation's clock and a fingerprint of its state (shard_time.h).
//
//   const auto t = Shard::Time::Now();
//   Send(Message{ t.fixedStep, ... });                    // a tick number two peers agree on
//   if (t.fixedStep % 60 == 0) Send(Shard::Time::Hash()); // compare it with the other peer's to know they stayed in step
//
// `simTime` and `fixedStep` are the simulation's (a replay reproduces them) ; `realTime` is the machine's, for what is not part of the game.
#pragma once

#include <shard/shard_time.h>
#include <shard/cpp/detail.hpp>

namespace Shard {

    struct TimeInfo {
        bool playing = false;           ///< play mode (always in a player)
        uint64_t frame = 0;
        uint64_t fixedStep = 0;         ///< fixed steps run so far : the tick number of a network protocol
        double simTime = 0.0;           ///< seconds of simulation, deterministic
        double realTime = 0.0;          ///< seconds since the engine started, on the wall clock : NOT deterministic
        float deltaTime = 0.f;
        float fixedDeltaTime = 0.f;
        float timeScale = 1.f;
        float fixedAlpha = 0.f;         ///< how far the frame sits between the last fixed step and the next one
    };

    /// What a run looks like at one moment, as hashes : two runs (or two peers) that stayed in step have the same ones
    struct StateHash {
        uint64_t frame = 0, physicsSteps = 0, transforms = 0, physics = 0, scripts = 0, total = 0;
        bool operator==(const StateHash& o) const { return total == o.total; }
        bool operator!=(const StateHash& o) const { return total != o.total; }
    };

    class Time {
    public:
        /// thread : any
        static TimeInfo Now()
        {
            ShardTimeInfo raw{};
            raw.structSize = sizeof(ShardTimeInfo);
            TimeInfo out;
            if (Api().get(&raw) != SHARD_OK) return out;
            out.playing = raw.playing == SHARD_TRUE;
            out.frame = raw.frame;
            out.fixedStep = raw.fixedStep;
            out.simTime = raw.simTime;
            out.realTime = raw.realTime;
            out.deltaTime = raw.deltaTime;
            out.fixedDeltaTime = raw.fixedDeltaTime;
            out.timeScale = raw.timeScale;
            out.fixedAlpha = raw.fixedAlpha;
            return out;
        }
        /// 0 pauses the simulation (the frame still runs). thread : main
        static bool SetTimeScale(float scale) { return Api().set_time_scale(scale) == SHARD_OK; }
        /// The fixed step, in seconds : a network game picks the tick rate it agreed on. thread : main
        static bool SetFixedDeltaTime(float seconds) { return Api().set_fixed_delta_time(seconds) == SHARD_OK; }
        /// Hashes the simulation now (between frames : on_frame_begin, a system of the Input or Extract phase). thread : main
        static StateHash Hash()
        {
            ShardStateHash raw{};
            raw.structSize = sizeof(ShardStateHash);
            StateHash out;
            if (Api().state_hash(&raw) != SHARD_OK) return out;
            out.frame = raw.frame; out.physicsSteps = raw.physicsSteps; out.transforms = raw.transforms;
            out.physics = raw.physics; out.scripts = raw.scripts; out.total = raw.total;
            return out;
        }

    private:
        static const ShardTimeApi& Api() { return detail::Require<ShardTimeApi>(SHARD_IFACE_TIME); }
    };
}
