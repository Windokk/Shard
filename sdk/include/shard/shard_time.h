/* Interface "shard.time" 1.0 : the simulation's clock, and a fingerprint of its state.
 *
 * A system or a script is handed the step it runs for (shard_systems.h, shard_scripts.h) ; this is for the rest : a particle emitter that
 * wants the simulation time to seed its randomness, a network layer that numbers its ticks, a UI that animates in real time.
 *
 * Two kinds of time. The simulation's (`simTime`, `fixedStep`) advances by whole fixed steps and only while the world plays : a replay of the
 * run reproduces it exactly, so anything that changes what the game looks like must use it. The wall clock (`realTime`) is the machine's :
 * for what is not part of the simulation (a loading spinner, a cursor blink, a network time-out) and never for what the game decides. The
 * `time_scale` of the engine (slow motion, pause) acts on the first and not on the second.
 *
 * thread : any, except the setters (main). */
#ifndef SHARD_TIME_H
#define SHARD_TIME_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_TIME "shard.time"

typedef struct ShardTimeInfo {
    uint32_t structSize;
    int32_t  playing;               /* SHARD_TRUE in play mode (always in a player) ; in the editor's edit mode the simulation does not step */
    uint64_t frame;                 /* frames run so far */
    uint64_t fixedStep;             /* fixed steps the simulation ran so far : the tick number of a network protocol */
    double   simTime;               /* seconds of simulation : fixedStep * fixedDeltaTime, deterministic */
    double   realTime;              /* seconds since the engine started, on the wall clock : NOT deterministic */
    float    deltaTime;             /* the last frame's variable step, in seconds, before the time scale */
    float    fixedDeltaTime;        /* the fixed step, in seconds */
    float    timeScale;             /* 1 : real time, 0 : paused, 0.5 : slow motion */
    float    fixedAlpha;            /* how far the frame sits between the last fixed step and the next one, in [0, 1) : what to interpolate with */
} ShardTimeInfo;

/* A fingerprint of the simulation after a frame : what two peers compare to know they stayed in step, and what a replay checks. One hash per
 * kind of state, so that the first difference says where to look. Every hash is the same on every platform when the run was. */
typedef struct ShardStateHash {
    uint32_t structSize;
    uint32_t reserved;
    uint64_t frame;
    uint64_t physicsSteps;
    uint64_t transforms;            /* position, rotation, scale of every actor of the loaded worlds */
    uint64_t physics;               /* pose, velocities and activity of every physics body */
    uint64_t scripts;               /* the declared fields of every script */
    uint64_t total;                 /* all of the above */
} ShardStateHash;

typedef struct ShardTimeApi {
    uint32_t structSize;

    /* The caller sets out->structSize ; the host fills what fits. */
    ShardResult (SHARD_CALL *get)(ShardTimeInfo* out);

    /* thread : main. 0 pauses the simulation (the frame still runs : UIs, animations in real time...), negative or not finite is
     * SHARD_E_INVALID_ARG. The scale is the user's to change too (a pause menu) : read it back with get before relying on it. */
    ShardResult (SHARD_CALL *set_time_scale)(float scale);
    /* thread : main. The fixed step, in seconds (1/60 by default) : a network game picks the tick rate it has agreed on. Takes effect on the
     * next frame. Changing it while peers run is the game's problem : every peer must change it on the same fixed step. */
    ShardResult (SHARD_CALL *set_fixed_delta_time)(float seconds);

    /* Hashes the simulation now (between frames : from on_frame_begin / on_frame_end, a system of the Input or Extract phase, or a job
     * waited for from there). Reads only. thread : main. SHARD_E_UNSUPPORTED : this host has no engine (an offline tool). */
    ShardResult (SHARD_CALL *state_hash)(ShardStateHash* out);
} ShardTimeApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_TIME_H */
