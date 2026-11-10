/* Interface "shard.timers" 1.0 : delays, repeating timers and coroutines, run by the simulation.
 *
 * A timer runs a callback later : once after a delay, or every so often. A coroutine is a callback that decides, each time it runs, how long
 * to sleep before running again (or that it is finished), which is how a sequence of "do this, wait 2 seconds, do that" is written without a
 * state machine in the game.
 *
 * Time is the simulation's : timers advance in the fixed step, only while the world plays, so the time scale (0 pauses them) acts on them and
 * a replay of the run reproduces them. The resolution is one fixed step : a timer runs on the first step at or after its due time, and a
 * repeating one at most once per step (a late timer does not replay what it missed). The callbacks run on the fixed step like a script's
 * (thread : script). A timer added from a callback first runs on the next step.
 *
 * Timers belong to the active world and die with it (when it stops or unloads), and with the plugin that made them : a timer never calls into
 * a plugin that was disabled. A handle is only good for its plugin. A plugin may have 16384 timers and 1024 latches at once, then the calls
 * that add one return SHARD_E_OUT_OF_MEMORY. Nothing can be added from a script's edit tick (SHARD_E_UNSUPPORTED) : the world does not
 * simulate there, so a timer would never run.
 *
 * thread : main, or a script's (is_active and latch_fired : any thread). */
#ifndef SHARD_TIMERS_H
#define SHARD_TIMERS_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_TIMERS "shard.timers"

typedef uint64_t ShardTimer;        /* 0 is never a timer */
typedef uint64_t ShardLatch;        /* 0 is never a latch */

enum {
    SHARD_WAIT_DONE = 0,            /* the coroutine is finished (what a step that sets nothing gets) */
    SHARD_WAIT_SECONDS = 1,         /* run again after `seconds` of simulation time (0 : on the next step) */
    SHARD_WAIT_LATCH = 2            /* run again once `latch` has fired, or after `seconds` if that is >= 0 (negative : no time-out) */
};

typedef struct ShardWait {
    int32_t   kind;                 /* SHARD_WAIT_* */
    int32_t   reserved;
    double    seconds;
    ShardLatch latch;
} ShardWait;

typedef void (SHARD_CALL *ShardTimerFn)(void* user);
/* One step of a coroutine. `wait` is preset to SHARD_WAIT_DONE : set it to ask to be run again. The first step runs on the step after start. */
typedef void (SHARD_CALL *ShardStepFn)(void* user, ShardWait* wait);

typedef struct ShardTimersApi {
    uint32_t structSize;

    /* Runs fn once after `seconds` (negative counts as 0). A seconds that is not finite is SHARD_E_INVALID_ARG. SHARD_E_UNSUPPORTED : no world is loaded. */
    ShardResult (SHARD_CALL *after)(ShardPluginCtx ctx, double seconds, ShardTimerFn fn, void* user, ShardTimer* out);
    /* Runs fn every `interval` seconds, the first time after one interval. */
    ShardResult (SHARD_CALL *every)(ShardPluginCtx ctx, double interval, ShardTimerFn fn, void* user, ShardTimer* out);
    ShardResult (SHARD_CALL *start)(ShardPluginCtx ctx, ShardStepFn step, void* user, ShardTimer* out);

    /* Safe from inside a callback, the running one included. SHARD_E_NOT_FOUND : finished, cancelled, or not this plugin's. */
    ShardResult (SHARD_CALL *cancel)(ShardPluginCtx ctx, ShardTimer timer);
    /* Cancels every timer of this plugin. */
    ShardResult (SHARD_CALL *cancel_all)(ShardPluginCtx ctx);
    int32_t     (SHARD_CALL *is_active)(ShardPluginCtx ctx, ShardTimer timer);

    /* A latch remembers that the named event (shard.events) was emitted at least once since the latch was made, so an event that comes before
     * the coroutine reaches its wait is not lost. Wait on it with SHARD_WAIT_LATCH, then ask latch_fired to tell the event from the time-out.
     * Released with the plugin, or with latch_release. */
    ShardResult (SHARD_CALL *event_latch)(ShardPluginCtx ctx, ShardStr name, ShardLatch* out);
    int32_t     (SHARD_CALL *latch_fired)(ShardPluginCtx ctx, ShardLatch latch);
    ShardResult (SHARD_CALL *latch_release)(ShardPluginCtx ctx, ShardLatch latch);
} ShardTimersApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_TIMERS_H */
