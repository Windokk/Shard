/* Interface "shard.input" 1.1 : keyboard, mouse and gamepads, polled ; and the window system's events, in order.
 *
 * What the engine's input manager sees this frame, which is also what a recorded run replays : a script that reads input through here
 * behaves in a replay as the game did. The key and button numbers below are part of the ABI (the engine maps them to its own).
 *
 * 1.1 added, from event_count on, what a UI needs and polling cannot say : the events of the frame in the order they happened (key, text,
 * mouse, wheel), the modifiers, the window's size, the clipboard, the cursor's shape, an IME's text box, and a way to say that a UI has taken
 * the mouse or the keyboard so that the game stops reacting to it. The events come from the window system, not from the recorded input : a
 * replay does not produce them, so use them for interfaces and never for what the simulation decides.
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

/* Modifier keys held (bits of ShardInputEvent::mods and of `modifiers`) */
#define SHARD_MOD_SHIFT         1u
#define SHARD_MOD_CONTROL       2u
#define SHARD_MOD_ALT           4u
#define SHARD_MOD_SUPER         8u

/* Kinds of event (1.1) */
#define SHARD_EVENT_KEY_DOWN        1u      /* key, mods ; flags REPEAT for a key held down */
#define SHARD_EVENT_KEY_UP          2u
#define SHARD_EVENT_TEXT            3u      /* codepoint : one Unicode character typed (an IME's text arrives one character at a time) */
#define SHARD_EVENT_MOUSE_DOWN      4u      /* key : the button, x, y in pixels of the window */
#define SHARD_EVENT_MOUSE_UP        5u
#define SHARD_EVENT_MOUSE_MOVE      6u      /* x, y ; dx, dy : the motion since the last event */
#define SHARD_EVENT_WHEEL           7u      /* dx, dy : lines scrolled (dy > 0 : away from the user) ; x, y : where the cursor is */
#define SHARD_EVENT_FOCUS_GAINED    8u
#define SHARD_EVENT_FOCUS_LOST      9u      /* release your held keys and buttons : no key up will come */
#define SHARD_EVENT_RESIZED         10u     /* x, y : the new size in pixels */

#define SHARD_EVENT_FLAG_REPEAT     1u

typedef struct ShardInputEvent {
    uint32_t structSize;
    uint32_t kind;                  /* SHARD_EVENT_* */
    int32_t  key;                   /* SHARD_KEY_* or SHARD_MOUSE_* */
    uint32_t mods;                  /* SHARD_MOD_* held when it happened */
    uint32_t flags;                 /* SHARD_EVENT_FLAG_* */
    uint32_t codepoint;
    float    x, y;
    float    dx, dy;
} ShardInputEvent;

/* Which polled input a UI takes (bits of `capture`) */
#define SHARD_CAPTURE_MOUSE     1u      /* mouse_down / pressed / released read false, get_cursor stays */
#define SHARD_CAPTURE_KEYBOARD  2u      /* key_down / pressed / released read false */
#define SHARD_CAPTURE_GAMEPAD   4u

/* The cursor's shapes (set_cursor_shape) */
#define SHARD_CURSOR_ARROW      0
#define SHARD_CURSOR_TEXT       1
#define SHARD_CURSOR_HAND       2
#define SHARD_CURSOR_CROSSHAIR  3
#define SHARD_CURSOR_RESIZE_EW  4
#define SHARD_CURSOR_RESIZE_NS  5
#define SHARD_CURSOR_RESIZE_NWSE 6
#define SHARD_CURSOR_RESIZE_NESW 7
#define SHARD_CURSOR_MOVE       8
#define SHARD_CURSOR_NOT_ALLOWED 9

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

    /* ---- 1.1 ---- */
    /* The events since the last frame began, in order. event_at fills *out (the caller sets structSize) ; SHARD_E_NOT_FOUND : past the end.
     * The list is the same for every plugin and stays valid for the whole frame. */
    uint32_t    (SHARD_CALL *event_count)(void);
    ShardResult (SHARD_CALL *event_at)(uint32_t index, ShardInputEvent* out);
    /* SHARD_MOD_* held now */
    uint32_t    (SHARD_CALL *modifiers)(void);
    /* The size of the window's drawing area in pixels, and its content scale (1 on a normal display, 2 on a "retina" one). Any of the
     * pointers may be NULL. In the editor this is the editor window, not the viewport the game is shown in : an overlay reads its own size from
     * ShardOverlayFrame (and the origin to subtract from the positions of the events). */
    ShardResult (SHARD_CALL *window_size)(int32_t* width, int32_t* height, float* dpi_scale);

    /* The clipboard's text (as a "text out") and setting it. SHARD_E_UNSUPPORTED : no window system (a server, the tests). */
    ShardResult (SHARD_CALL *clipboard_get)(char* buffer, uint64_t capacity, uint64_t* size);
    ShardResult (SHARD_CALL *clipboard_set)(ShardStr text);

    /* The cursor's shape for this frame (SHARD_CURSOR_*) : it goes back to the arrow next frame unless it is set again. */
    ShardResult (SHARD_CALL *set_cursor_shape)(int32_t shape);
    /* A text field has the focus : the system may show its on screen keyboard or an IME's candidate window next to the box x, y, w, h (pixels of
     * the window). enabled SHARD_FALSE when it loses it. */
    ShardResult (SHARD_CALL *set_text_input)(int32_t enabled, int32_t x, int32_t y, int32_t width, int32_t height);

    /* The UI says which input it takes this frame (SHARD_CAPTURE_* bits ; 0 releases it). While any plugin captures, the polled functions above
     * read false for what was captured -- for every script, the capturing plugin's own included, which reads the events instead. Call it every
     * frame from the UI's update : a plugin that stops calling lets go after one frame. */
    ShardResult (SHARD_CALL *capture)(ShardPluginCtx ctx, uint32_t mask);
} ShardInputApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_INPUT_H */
