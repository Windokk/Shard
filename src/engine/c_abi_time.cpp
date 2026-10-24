#include <cmath>
#include <cstring>

#include <shard/shard_time.h>

#include "engine/physics/physics_manager.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/simulation_state.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/time_manager.hpp"

// shard.time : the simulation's clock and a fingerprint of its state. Lives in the engine root : the fingerprint (simulation_state.hpp) is the root's.

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        ShardResult SHARD_CALL Get(ShardTimeInfo* out)
        {
            try {
                if (!out || out->structSize < sizeof(uint32_t)) return SHARD_E_INVALID_ARG;
                Core::IEngineContext* engine = Core::gSharedEnginePtr;
                Time::TimeManager* time = engine ? engine->GetTimeManager() : nullptr;
                if (!engine || !time) return SHARD_E_UNSUPPORTED;

                ShardTimeInfo info{};
                info.structSize = sizeof(ShardTimeInfo);
                info.playing = engine->IsInPlayMode() ? SHARD_TRUE : SHARD_FALSE;
                info.frame = engine->GetFrameNumber();
                const Physics::PhysicsManager* physics = engine->GetPhysicsManager();
                info.fixedStep = physics ? physics->GetStepCount() : 0;
                info.fixedDeltaTime = time->GetFixedDeltaTime();
                info.simTime = double(info.fixedStep) * double(info.fixedDeltaTime);
                const Time::TimeStamp app = time->CurrentAppTime();
                info.realTime = double(app.days) * 86400.0 + double(app.hours) * 3600.0 + double(app.minutes) * 60.0 + double(app.seconds) + double(app.milliseconds) * 1e-3 + double(app.nanoseconds) * 1e-9;
                info.deltaTime = time->GetDeltaTime();
                info.timeScale = time->GetTimeSpeed();
                info.fixedAlpha = time->GetFixedStepAlpha();

                const uint32_t asked = out->structSize;
                std::memcpy(out, &info, std::min<uint32_t>(asked, sizeof(ShardTimeInfo)));
                out->structSize = asked;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetTimeScale(float scale)
        {
            try {
                if (!std::isfinite(scale) || scale < 0.0f) return SHARD_E_INVALID_ARG;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                Core::IEngineContext* engine = Core::gSharedEnginePtr;
                Time::TimeManager* time = engine ? engine->GetTimeManager() : nullptr;
                if (!time) return SHARD_E_UNSUPPORTED;
                time->SetTimeSpeed(scale);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetFixedDeltaTime(float seconds)
        {
            try {
                if (!std::isfinite(seconds) || seconds < 1.0f / 1000.0f || seconds > 1.0f) return SHARD_E_INVALID_ARG;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                Core::IEngineContext* engine = Core::gSharedEnginePtr;
                Time::TimeManager* time = engine ? engine->GetTimeManager() : nullptr;
                if (!time) return SHARD_E_UNSUPPORTED;
                time->SetFixedDeltaTime(seconds);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL StateHash(ShardStateHash* out)
        {
            try {
                if (!out || out->structSize < sizeof(uint32_t)) return SHARD_E_INVALID_ARG;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                Core::IEngineContext* engine = Core::gSharedEnginePtr;
                if (!engine) return SHARD_E_UNSUPPORTED;
                const Core::SimulationStateHash h = Core::HashSimulationState(*engine);
                ShardStateHash info{};
                info.structSize = sizeof(ShardStateHash);
                info.frame = h.frame;
                info.physicsSteps = h.physicsSteps;
                info.transforms = h.transforms;
                info.physics = h.physics;
                info.scripts = h.scripts;
                info.total = h.total;
                const uint32_t n = std::min<uint32_t>(out->structSize, sizeof(ShardStateHash));
                const uint32_t asked = out->structSize;
                std::memcpy(out, &info, n);
                out->structSize = asked;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    const void* TimeApi()
    {
        static const ShardTimeApi api = { sizeof(ShardTimeApi), &Get, &SetTimeScale, &SetFixedDeltaTime, &StateHash };
        return &api;
    }
}
