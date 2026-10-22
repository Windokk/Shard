/* Interface "shard.input" 1.0 : keyboard, mouse and gamepads, polled.
 *
 * What the engine's input manager sees this frame, which is also what a recorded run replays : a script that reads input through here
 * behaves in a replay as the game did. The key and button numbers below are part of the ABI (the engine maps them to its own).
 *
 * thread : script, for every function (shard_actors.h says where that is). */
#ifndef SHARD_INPUT_H
#define SHARD_INPUT_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_INPUT "shard.input"

/* Keys : the numbers of the common keyboard APIs (printable keys are their upper case ASCII) */
#define SHARD_KEY_SPACE         32
#define SHARD_KEY_APOSTROPHE    39
#define SHARD_KEY_COMMA         44
#define SHARD_KEY_MINUS         45
#define SHARD_KEY_PERIOD        46
#define SHARD_KEY_SLASH         47
#define SHARD_KEY_0             48      /* ... SHARD_KEY_9 = 57 */
#define SHARD_KEY_SEMICOLON     59
#define SHARD_KEY_EQUAL         61
#define SHARD_KEY_A             65      /* ... SHARD_KEY_Z = 90 */
#define SHARD_KEY_LEFT_BRACKET  91
#define SHARD_KEY_BACKSLASH     92
#define SHARD_KEY_RIGHT_BRACKET 93
#define SHARD_KEY_GRAVE_ACCENT  96
#define SHARD_KEY_ESCAPE        256
#define SHARD_KEY_ENTER         257
#define SHARD_KEY_TAB           258
#define SHARD_KEY_BACKSPACE     259
#define SHARD_KEY_INSERT        260
#define SHARD_KEY_DELETE        261
#define SHARD_KEY_RIGHT         262
#define SHARD_KEY_LEFT          263
#define SHARD_KEY_DOWN          264
#define SHARD_KEY_UP            265
#define SHARD_KEY_PAGE_UP       266
#define SHARD_KEY_PAGE_DOWN     267
#define SHARD_KEY_HOME          268
#define SHARD_KEY_END           269
#define SHARD_KEY_CAPS_LOCK     280
#define SHARD_KEY_SCROLL_LOCK   281
#define SHARD_KEY_NUM_LOCK      282
#define SHARD_KEY_PRINT_SCREEN  283
#define SHARD_KEY_PAUSE         284
#define SHARD_KEY_F1            290     /* ... SHARD_KEY_F25 = 314 */
#define SHARD_KEY_KP_0          320     /* ... SHARD_KEY_KP_9 = 329 */
#define SHARD_KEY_KP_DECIMAL    330
#define SHARD_KEY_KP_DIVIDE     331
#define SHARD_KEY_KP_MULTIPLY   332
#define SHARD_KEY_KP_SUBTRACT   333
#define SHARD_KEY_KP_ADD        334
#define SHARD_KEY_KP_ENTER      335
#define SHARD_KEY_KP_EQUAL      336
#define SHARD_KEY_LEFT_SHIFT    340
#define SHARD_KEY_LEFT_CONTROL  341
#define SHARD_KEY_LEFT_ALT      342
#define SHARD_KEY_LEFT_SUPER    343
#define SHARD_KEY_RIGHT_SHIFT   344
#define SHARD_KEY_RIGHT_CONTROL 345
#define SHARD_KEY_RIGHT_ALT     346
#define SHARD_KEY_RIGHT_SUPER   347
#define SHARD_KEY_MENU          348

/* Mouse buttons */
#define SHARD_MOUSE_LEFT        0
#define SHARD_MOUSE_RIGHT       1
#define SHARD_MOUSE_MIDDLE      2       /* buttons 3 to 7 follow */

/* The cursor */
#define SHARD_CURSOR_VISIBLE    0
#define SHARD_CURSOR_HIDDEN     1
#define SHARD_CURSOR_DISABLED   2       /* hidden and locked to the window : the mouse look of a first person game */

/* Gamepads : the face buttons are named by position (SOUTH is A on an Xbox pad, Cross on a PlayStation one) */
#define SHARD_PAD_SOUTH         0
#define SHARD_PAD_EAST          1
#define SHARD_PAD_WEST          2
#define SHARD_PAD_NORTH         3
#define SHARD_PAD_BACK          4
#define SHARD_PAD_GUIDE         5
#define SHARD_PAD_START         6
#define SHARD_PAD_LEFT_STICK    7
#define SHARD_PAD_RIGHT_STICK   8
#define SHARD_PAD_LEFT_SHOULDER 9
#define SHARD_PAD_RIGHT_SHOULDER 10
#define SHARD_PAD_DPAD_UP       11
#define SHARD_PAD_DPAD_DOWN     12
#define SHARD_PAD_DPAD_LEFT     13
#define SHARD_PAD_DPAD_RIGHT    14

#define SHARD_AXIS_LEFT_X       0       /* sticks -1 (left, up) to 1 (right, down) */
#define SHARD_AXIS_LEFT_Y       1
#define SHARD_AXIS_RIGHT_X      2
#define SHARD_AXIS_RIGHT_Y      3
#define SHARD_AXIS_LEFT_TRIGGER 4       /* triggers 0 to 1 */
#define SHARD_AXIS_RIGHT_TRIGGER 5

typedef struct ShardInputApi {
    uint32_t structSize;

    /* Keyboard : SHARD_TRUE / SHARD_FALSE. `pressed` and `released` are true on the frame the key changes, `down` while it is held. */
    int32_t (SHARD_CALL *key_down)(int32_t key);
    int32_t (SHARD_CALL *key_pressed)(int32_t key);
    int32_t (SHARD_CALL *key_released)(int32_t key);

    /* Mouse */
    int32_t (SHARD_CALL *mouse_down)(int32_t button);
    int32_t (SHARD_CALL *mouse_pressed)(int32_t button);
    int32_t (SHARD_CALL *mouse_released)(int32_t button);
    ShardResult (SHARD_CALL *get_cursor)(double* x, double* y);
    ShardResult (SHARD_CALL *set_cursor)(double x, double y);
    ShardResult (SHARD_CALL *set_cursor_mode)(int32_t mode);

    /* Gamepads, numbered from 0 in the order they were plugged in */
    int32_t (SHARD_CALL *gamepad_count)(void);
    int32_t (SHARD_CALL *gamepad_button_down)(int32_t pad, int32_t button);
    int32_t (SHARD_CALL *gamepad_button_pressed)(int32_t pad, int32_t button);
    int32_t (SHARD_CALL *gamepad_button_released)(int32_t pad, int32_t button);
    /* Dead zone : the fraction of the range that reads as 0 (0.1 is a fine default) */
    float   (SHARD_CALL *gamepad_axis)(int32_t pad, int32_t axis, float dead_zone);
    /* Strengths 0 to 1. SHARD_FALSE : the pad cannot rumble. */
    int32_t (SHARD_CALL *gamepad_rumble)(int32_t pad, float low, float high, int32_t duration_ms);
} ShardInputApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_INPUT_H */
