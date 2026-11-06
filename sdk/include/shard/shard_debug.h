/* Interface "shard.debug" 1.0 : drawing for the developer, in the world, from anywhere : a navigation mesh, the path an agent follows, a field of
 * view, a voxel chunk's bounds, a bone, a network ghost, the node a graph is running.
 *
 * Unlike the gizmos of shard.editor (which draw in the editor, for the selection), this draws in the game's viewport and in the editor's, from
 * a system, a script or a job. Every call adds a shape to this frame's picture ; a shape with a `duration` stays that many seconds (real time),
 * with 0 it is drawn for one frame only, so a system that draws what it computes calls the functions again each frame and never clears anything.
 *
 * The shapes are projected and drawn over the picture by the host's overlay. SHARD_DEBUG_DEPTH_TEST asks that the world's geometry hides them ;
 * a host that cannot do it draws them over everything. Nothing here costs anything in a build that has no debug drawing (a player of a
 * shipped game may turn it off) : the calls are accepted and draw nothing.
 *
 * Colours are 8 bit r, g, b, a packed as r | g << 8 | b << 16 | a << 24. Positions are in the world ; a large world's are doubles in the
 * rest of the ABI, here floats relative to the camera's region are enough for drawing and the host rebases them.
 *
 * thread : any. A sandboxed plugin (tier 2) is not offered this interface. */
#ifndef SHARD_DEBUG_H
#define SHARD_DEBUG_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_DEBUG "shard.debug"

#define SHARD_DEBUG_DEPTH_TEST  0x01u   /* hidden by the geometry in front of it, where the host can */

typedef struct ShardDebugApi {
    uint32_t structSize;

    /* SHARD_FALSE when this host draws nothing (a server, a shipped player with debug drawing off) : skip the work of computing shapes. */
    int32_t (SHARD_CALL *enabled)(void);

    void (SHARD_CALL *line)(const float from[3], const float to[3], uint32_t color, float duration, uint32_t flags);
    /* Segments : `count` points (xyz xyz ...), taken two by two -- points 0 and 1 are a segment, 2 and 3 the next one. A polyline of n points is
     * n - 1 segments : the caller repeats the shared points. A navigation mesh's edges go in one call. */
    void (SHARD_CALL *lines)(const float* points, uint32_t count, uint32_t color, float duration, uint32_t flags);
    void (SHARD_CALL *point)(const float position[3], float size, uint32_t color, float duration, uint32_t flags);
    void (SHARD_CALL *arrow)(const float from[3], const float to[3], uint32_t color, float duration, uint32_t flags);
    void (SHARD_CALL *circle)(const float center[3], const float normal[3], float radius, uint32_t color, float duration, uint32_t flags);
    void (SHARD_CALL *sphere)(const float center[3], float radius, uint32_t color, float duration, uint32_t flags);
    /* A box turned by `rotation` (quaternion x, y, z, w) ; NULL : aligned on the axes. */
    void (SHARD_CALL *box)(const float center[3], const float half_extent[3], const float rotation[4], uint32_t color, float duration, uint32_t flags);
    /* A capsule between two points (the centres of its end spheres). */
    void (SHARD_CALL *capsule)(const float from[3], const float to[3], float radius, uint32_t color, float duration, uint32_t flags);
    /* A cone : its apex, the direction and length of its axis, the angle of its half opening in degrees (a field of view, a light). */
    void (SHARD_CALL *cone)(const float apex[3], const float axis[3], float half_angle_degrees, uint32_t color, float duration, uint32_t flags);
    /* Filled triangles, `count` of them (9 floats each) : a navigation mesh's polygons, a volume's faces. The colour's alpha is honoured. */
    void (SHARD_CALL *triangles)(const float* vertices, uint32_t count, uint32_t color, float duration, uint32_t flags);
    /* Text anchored at a point of the world, or at a pixel of the viewport (x, y from the top left). */
    void (SHARD_CALL *text)(const float position[3], ShardStr text, uint32_t color, float duration, uint32_t flags);
    void (SHARD_CALL *text_screen)(float x, float y, ShardStr text, uint32_t color, float duration);

    /* Removes every shape that is still being shown for a duration (the ones of this frame go anyway). */
    void (SHARD_CALL *clear)(void);
} ShardDebugApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_DEBUG_H */
