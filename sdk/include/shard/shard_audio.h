/* Interface "shard.audio" 1.0 : sounds.
 *
 * Two ways to make a sound. An actor with an "audio" component (add it with shard.actors' add_component, its clip, volume and 3D settings
 * are fields of the component) is played and paused here. A one shot sound needs no actor : it plays once from a place and removes itself
 * when it ends.
 *
 * A clip is an asset of the project, named by its path in the project ("sounds/shot.wav"). Volumes are 1 for the clip's own level.
 *
 * thread : script (shard_actors.h says where that is). SHARD_E_UNSUPPORTED : this host has no audio (an offline tool). A host that runs
 * with no sound device (a server, the tests) accepts every call and plays nothing. */
#ifndef SHARD_AUDIO_H
#define SHARD_AUDIO_H

#include "shard_abi.h"
#include "shard_actors.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_AUDIO "shard.audio"

typedef struct ShardAudioApi {
    uint32_t structSize;

    /* Starts the clip of the actor's audio component from the beginning (it does nothing if it already plays) */
    ShardResult (SHARD_CALL *play)(ShardActor actor);
    ShardResult (SHARD_CALL *pause)(ShardActor actor);
    ShardResult (SHARD_CALL *set_volume)(ShardActor actor, float volume);

    /* Plays `clip` once, from `position` in the world when `spatial` is SHARD_TRUE (louder and panned by the distance and direction to the
     * listener), flat otherwise. SHARD_E_NOT_FOUND : no such clip. */
    ShardResult (SHARD_CALL *play_one_shot)(ShardStr clip, const float position[3], float volume, int32_t spatial);
} ShardAudioApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_AUDIO_H */
