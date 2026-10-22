/* Interface "shard.editor" 1.0 : windows of the editor, with a small set of widgets.
 *
 * ImGui has no stable ABI, so a plugin does not draw with it : it adds a panel and draws the contents with the widgets of ShardUiApi, which
 * the editor implements with its own ImGui. The panel is a window of the editor's Window menu (hidden until the user opens it). Its draw
 * function runs every frame while the window is open, on the editor's main thread, and may call the widgets then and only then.
 *
 * This interface exists only in the editor : in the player, in headless runs and in the offline tools query_interface answers
 * SHARD_E_NOT_FOUND, so a plugin that has panels keeps working there. The editor loads a plugin whose manifest "hosts" include "engine" (the default) or "editor".
 *
 * Widgets that take a value change it in place and return SHARD_TRUE on the frame the user changed it. A label may carry a hidden id after
 * "##" ("Speed##enemy1"), as in ImGui, to tell apart two widgets with the same text. */
#ifndef SHARD_EDITOR_H
#define SHARD_EDITOR_H

#include "shard_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_EDITOR "shard.editor"

typedef struct ShardUiApi {
    uint32_t structSize;

    /* Text */
    void    (SHARD_CALL *text)(ShardStr text);
    void    (SHARD_CALL *text_colored)(const float rgba[4], ShardStr text);
    void    (SHARD_CALL *text_wrapped)(ShardStr text);
    void    (SHARD_CALL *bullet_text)(ShardStr text);
    void    (SHARD_CALL *label_text)(ShardStr label, ShardStr value);

    /* Buttons and choices */
    int32_t (SHARD_CALL *button)(ShardStr label);
    int32_t (SHARD_CALL *small_button)(ShardStr label);
    int32_t (SHARD_CALL *checkbox)(ShardStr label, int32_t* value);
    int32_t (SHARD_CALL *selectable)(ShardStr label, int32_t selected);
    /* `current` is the index of the chosen item of `items` */
    int32_t (SHARD_CALL *combo)(ShardStr label, int32_t* current, const ShardStr* items, uint32_t count);

    /* Values */
    int32_t (SHARD_CALL *slider_float)(ShardStr label, float* value, float min, float max);
    int32_t (SHARD_CALL *slider_int)(ShardStr label, int32_t* value, int32_t min, int32_t max);
    int32_t (SHARD_CALL *drag_float)(ShardStr label, float* value, float speed, float min, float max);
    int32_t (SHARD_CALL *drag_int)(ShardStr label, int32_t* value, float speed, int32_t min, int32_t max);
    int32_t (SHARD_CALL *input_float)(ShardStr label, float* value);
    int32_t (SHARD_CALL *input_int)(ShardStr label, int32_t* value);
    /* `buffer` holds a zero terminated text of at most capacity - 1 bytes, edited in place */
    int32_t (SHARD_CALL *input_text)(ShardStr label, char* buffer, uint64_t capacity);
    int32_t (SHARD_CALL *color_edit)(ShardStr label, float rgba[4]);

    /* Display */
    void    (SHARD_CALL *progress_bar)(float fraction, ShardStr overlay);
    void    (SHARD_CALL *plot_lines)(ShardStr label, const float* values, int32_t count);

    /* Layout */
    void    (SHARD_CALL *separator)(void);
    void    (SHARD_CALL *same_line)(void);
    void    (SHARD_CALL *spacing)(void);
    void    (SHARD_CALL *set_next_item_width)(float width);
    /* SHARD_TRUE while the section is open ; the contents are drawn only then */
    int32_t (SHARD_CALL *collapsing_header)(ShardStr label);
    /* A node of a tree : when it returns SHARD_TRUE, draw the children and call tree_pop */
    int32_t (SHARD_CALL *tree_node)(ShardStr label);
    void    (SHARD_CALL *tree_pop)(void);
    void    (SHARD_CALL *push_id)(ShardStr id);
    void    (SHARD_CALL *pop_id)(void);

    /* The widget drawn last */
    int32_t (SHARD_CALL *is_item_hovered)(void);
    void    (SHARD_CALL *tooltip)(ShardStr text);
} ShardUiApi;

typedef void (SHARD_CALL *ShardPanelFn)(void* user, const ShardUiApi* ui);

typedef struct ShardEditorApi {
    uint32_t structSize;

    /* Adds a panel named `title` to the editor's Window menu, from on_load. It goes with the plugin. thread : main.
     * SHARD_E_ALREADY_EXISTS : this plugin has a panel of that name. */
    ShardResult (SHARD_CALL *add_panel)(ShardPluginCtx ctx, ShardStr title, ShardPanelFn draw, void* user);
} ShardEditorApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_EDITOR_H */
