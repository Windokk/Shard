/* Interface "shard.editor" 1.2 : the editor's windows, menus, toolbar, inspectors and gizmos, drawn with a small set of widgets.
 *
 * ImGui has no stable ABI, so a plugin does not draw with it : it adds a panel and draws the contents with the widgets of ShardUiApi, which
 * the editor implements with its own ImGui. The panel is a window of the editor's Window menu (hidden until the user opens it). Its draw
 * function runs every frame while the window is open, on the editor's main thread, and may call the widgets then and only then. The same
 * widgets draw an inspector (below a component in the Properties panel) ; ShardGizmoApi draws over the viewport.
 *
 * This interface exists only in the editor : in the player, in headless runs and in the offline tools query_interface answers
 * SHARD_E_NOT_FOUND, so a plugin that has panels keeps working there. The editor loads a plugin whose manifest "hosts" include "engine" (the default) or "editor".
 * A sandboxed plugin (tier 2) needs the permission "editor" in its manifest.
 *
 * Widgets that take a value change it in place and return SHARD_TRUE on the frame the user changed it. A label may carry a hidden id after
 * "##" ("Speed##enemy1"), as in ImGui, to tell apart two widgets with the same text.
 *
 * 1.1 added to ShardUiApi (from text_disabled on) : more text, vectors, multiline text, groups, children, disabled sections, tabs, tables, menus,
 * popups, item queries, and to ShardEditorApi (from add_menu_item on) : menu items, toolbar buttons, inspectors, gizmos, the selection, notifications.
 *
 * 1.2 added to ShardUiApi (from draw_line on) what a node editor, a timeline or a curve editor needs : a draw list (lines, shapes, Bezier curves,
 * text, clipping) in the window's coordinates, the cursor and mouse queries to build interactions on top of it, hit areas, images, and drag and
 * drop ; and to ShardEditorApi (from undo_push on) : the undo history, opening an asset of a plugin's type with a double click, and the entries
 * of the content browser's "Create" menu. A graph editor (visual scripting, behaviour trees, animation graphs) is made of exactly these. */
#ifndef SHARD_EDITOR_H
#define SHARD_EDITOR_H

#include "shard_abi.h"
#include "shard_actors.h"

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

    /* ---- 1.1 ---- */
    /* More text and vectors */
    void    (SHARD_CALL *text_disabled)(ShardStr text);
    void    (SHARD_CALL *separator_text)(ShardStr label);
    /* A radio button that is on when *value == button_value, and sets it so when clicked */
    int32_t (SHARD_CALL *radio_button)(ShardStr label, int32_t* value, int32_t button_value);
    int32_t (SHARD_CALL *drag_float2)(ShardStr label, float value[2], float speed, float min, float max);
    int32_t (SHARD_CALL *drag_float3)(ShardStr label, float value[3], float speed, float min, float max);
    int32_t (SHARD_CALL *drag_float4)(ShardStr label, float value[4], float speed, float min, float max);
    int32_t (SHARD_CALL *color_edit3)(ShardStr label, float rgb[3]);
    /* A text box of `width` x `height` pixels (0 : the available width, three lines) */
    int32_t (SHARD_CALL *input_text_multiline)(ShardStr label, char* buffer, uint64_t capacity, float width, float height);
    int32_t (SHARD_CALL *button_size)(ShardStr label, float width, float height);
    void    (SHARD_CALL *plot_histogram)(ShardStr label, const float* values, int32_t count);

    /* Layout */
    void    (SHARD_CALL *begin_group)(void);
    void    (SHARD_CALL *end_group)(void);
    void    (SHARD_CALL *indent)(float width);
    void    (SHARD_CALL *unindent)(float width);
    void    (SHARD_CALL *dummy)(float width, float height);
    void    (SHARD_CALL *new_line)(void);
    void    (SHARD_CALL *push_item_width)(float width);
    void    (SHARD_CALL *pop_item_width)(void);
    /* A scrolling region of its own : draw only when it returns SHARD_TRUE, and always call end_child */
    int32_t (SHARD_CALL *begin_child)(ShardStr id, float width, float height, int32_t border);
    void    (SHARD_CALL *end_child)(void);
    /* What is drawn between is greyed out and ignores the mouse while `disabled` is SHARD_TRUE */
    void    (SHARD_CALL *begin_disabled)(int32_t disabled);
    void    (SHARD_CALL *end_disabled)(void);
    void    (SHARD_CALL *push_text_color)(const float rgba[4]);
    void    (SHARD_CALL *pop_text_color)(void);
    float   (SHARD_CALL *content_width)(void);

    /* Tabs : draw the contents of a tab only when begin_tab_item returns SHARD_TRUE, and call end_tab_item then ; end_tab_bar after begin_tab_bar returned SHARD_TRUE */
    int32_t (SHARD_CALL *begin_tab_bar)(ShardStr id);
    void    (SHARD_CALL *end_tab_bar)(void);
    int32_t (SHARD_CALL *begin_tab_item)(ShardStr label);
    void    (SHARD_CALL *end_tab_item)(void);

    /* Tables : end_table after begin_table returned SHARD_TRUE. table_next_column returns SHARD_TRUE while the column is visible. */
    int32_t (SHARD_CALL *begin_table)(ShardStr id, int32_t columns);
    void    (SHARD_CALL *end_table)(void);
    void    (SHARD_CALL *table_setup_column)(ShardStr label);
    void    (SHARD_CALL *table_headers_row)(void);
    void    (SHARD_CALL *table_next_row)(void);
    int32_t (SHARD_CALL *table_next_column)(void);

    /* Menus (inside a popup or a menu) and popups. begin_menu / begin_popup : call the matching end only when they returned SHARD_TRUE. */
    int32_t (SHARD_CALL *begin_menu)(ShardStr label);
    void    (SHARD_CALL *end_menu)(void);
    int32_t (SHARD_CALL *menu_item)(ShardStr label, ShardStr shortcut, int32_t selected, int32_t enabled);
    void    (SHARD_CALL *open_popup)(ShardStr id);
    int32_t (SHARD_CALL *begin_popup)(ShardStr id);
    void    (SHARD_CALL *end_popup)(void);
    void    (SHARD_CALL *close_current_popup)(void);
    /* A popup opened by a right click on the widget drawn last */
    int32_t (SHARD_CALL *begin_popup_context_item)(void);

    /* The widget drawn last : `button` is 0 for the left mouse button, 1 for the right one */
    int32_t (SHARD_CALL *is_item_clicked)(int32_t button);
    int32_t (SHARD_CALL *is_item_active)(void);
    int32_t (SHARD_CALL *is_item_double_clicked)(void);

    /* ---- 1.2 ----
     * Positions are in pixels of the editor's window ("screen" coordinates, the origin at the top left of the main window), the same ones
     * get_cursor_screen_pos and get_mouse_pos give, so a canvas is drawn at what it was told. Colours are r | g << 8 | b << 16 | a << 24. */

    /* The draw list of the window being drawn. Shapes are clipped to the window and to the clip rectangles pushed. */
    void    (SHARD_CALL *draw_line)(const float a[2], const float b[2], uint32_t color, float thickness);
    void    (SHARD_CALL *draw_polyline)(const float* points, uint32_t count, uint32_t color, int32_t closed, float thickness);
    void    (SHARD_CALL *draw_rect)(const float min[2], const float max[2], uint32_t color, float rounding, float thickness);
    void    (SHARD_CALL *draw_rect_filled)(const float min[2], const float max[2], uint32_t color, float rounding);
    void    (SHARD_CALL *draw_circle)(const float center[2], float radius, uint32_t color, float thickness);
    void    (SHARD_CALL *draw_circle_filled)(const float center[2], float radius, uint32_t color);
    void    (SHARD_CALL *draw_triangle_filled)(const float a[2], const float b[2], const float c[2], uint32_t color);
    /* A cubic Bezier curve from p0 to p3 : the wire between two nodes */
    void    (SHARD_CALL *draw_bezier)(const float p0[2], const float p1[2], const float p2[2], const float p3[2], uint32_t color, float thickness);
    void    (SHARD_CALL *draw_text)(const float position[2], uint32_t color, ShardStr text);
    /* Nothing is drawn outside the rectangle until pop_clip_rect. intersect SHARD_TRUE : cut down to the current clip too. */
    void    (SHARD_CALL *push_clip_rect)(const float min[2], const float max[2], int32_t intersect);
    void    (SHARD_CALL *pop_clip_rect)(void);

    /* Where the next widget goes, the room left, and the rectangle of the widget drawn last */
    void    (SHARD_CALL *get_cursor_screen_pos)(float out[2]);
    void    (SHARD_CALL *set_cursor_screen_pos)(const float position[2]);
    void    (SHARD_CALL *get_content_region_avail)(float out[2]);
    void    (SHARD_CALL *get_item_rect_min)(float out[2]);
    void    (SHARD_CALL *get_item_rect_max)(float out[2]);
    void    (SHARD_CALL *calc_text_size)(ShardStr text, float out[2]);
    /* A hit area of that size at the cursor, with no look of its own : the surface of a canvas or a node. SHARD_TRUE on the frame it is
     * pressed ; is_item_hovered, is_item_active... then tell the rest. */
    int32_t (SHARD_CALL *invisible_button)(ShardStr id, float width, float height);

    /* The mouse and keyboard, as the window sees them this frame. `button` : SHARD_MOUSE_*, `key` : SHARD_KEY_*. */
    void    (SHARD_CALL *get_mouse_pos)(float out[2]);
    void    (SHARD_CALL *get_mouse_drag_delta)(int32_t button, float out[2]);
    void    (SHARD_CALL *reset_mouse_drag_delta)(int32_t button);
    float   (SHARD_CALL *get_mouse_wheel)(void);
    int32_t (SHARD_CALL *is_mouse_down)(int32_t button);
    int32_t (SHARD_CALL *is_mouse_clicked)(int32_t button);
    int32_t (SHARD_CALL *is_mouse_released)(int32_t button);
    int32_t (SHARD_CALL *is_mouse_double_clicked)(int32_t button);
    int32_t (SHARD_CALL *is_key_down)(int32_t key);
    int32_t (SHARD_CALL *is_key_pressed)(int32_t key);
    /* SHARD_MOD_* (shard_input.h) held now */
    uint32_t (SHARD_CALL *get_modifiers)(void);
    int32_t (SHARD_CALL *is_window_hovered)(void);
    int32_t (SHARD_CALL *is_window_focused)(void);

    /* A texture of shard.render (a preview of a UI document, of a mesh being animated, of an atlas) of that size in pixels */
    void    (SHARD_CALL *image)(uint64_t texture, float width, float height);
    int32_t (SHARD_CALL *selectable_size)(ShardStr label, int32_t selected, float width, float height);

    /* Drag and drop between widgets (and from the content browser : a payload of type "shard.asset" is the asset's path in the project, as
     * UTF-8). Source : after the widget, begin_drag_drop_source ; when it returns SHARD_TRUE set the payload and call end_drag_drop_source.
     * Target : after the widget, begin_drag_drop_target ; when SHARD_TRUE ask for the payloads it takes with accept_drag_drop_payload (SHARD_TRUE
     * on the frame it is dropped, then `out` holds it) and call end_drag_drop_target. `type` is at most 32 characters. */
    int32_t (SHARD_CALL *begin_drag_drop_source)(void);
    void    (SHARD_CALL *set_drag_drop_payload)(ShardStr type, const void* data, uint64_t size);
    void    (SHARD_CALL *end_drag_drop_source)(void);
    int32_t (SHARD_CALL *begin_drag_drop_target)(void);
    int32_t (SHARD_CALL *accept_drag_drop_payload)(ShardStr type, void* out, uint64_t capacity, uint64_t* size);
    void    (SHARD_CALL *end_drag_drop_target)(void);
} ShardUiApi;

/* What an inspector or a gizmo draws with over the viewport (ShardGizmoFn) : lines in the world, in the units of the project, with a colour
 * r, g, b, a in 0..1. Valid during the callback only. */
typedef struct ShardGizmoApi {
    uint32_t structSize;
    void (SHARD_CALL *line)(const float from[3], const float to[3], const float rgba[4]);
    void (SHARD_CALL *point)(const float position[3], float size, const float rgba[4]);
    /* A circle around `center` in the plane whose normal is `normal` */
    void (SHARD_CALL *circle)(const float center[3], const float normal[3], float radius, const float rgba[4]);
    /* Three circles, one per axis */
    void (SHARD_CALL *sphere)(const float center[3], float radius, const float rgba[4]);
    /* A box aligned on the world's axes */
    void (SHARD_CALL *box)(const float center[3], const float half_extent[3], const float rgba[4]);
    /* A line with a head at `to` */
    void (SHARD_CALL *arrow)(const float from[3], const float to[3], const float rgba[4]);
    void (SHARD_CALL *text)(const float position[3], ShardStr text, const float rgba[4]);
} ShardGizmoApi;

typedef void (SHARD_CALL *ShardPanelFn)(void* user, const ShardUiApi* ui);
/* A menu item or a toolbar button was clicked */
typedef void (SHARD_CALL *ShardEditorActionFn)(void* user);
/* Drawn below the component of that type in the Properties panel, for the actor that is shown there. `ui` is the panel's widgets. */
typedef void (SHARD_CALL *ShardInspectorFn)(void* user, const ShardUiApi* ui, ShardActor actor);
/* Called each frame for every selected actor, while the viewport is drawn */
typedef void (SHARD_CALL *ShardGizmoFn)(void* user, const ShardGizmoApi* gizmo, ShardActor actor);
/* The selection changed : the selected actors, the primary one (the last selected) last. Valid for the call. */
typedef void (SHARD_CALL *ShardSelectionFn)(void* user, const ShardActor* actors, uint32_t count);
/* Undo or redo of a change the plugin recorded with undo_push */
typedef void (SHARD_CALL *ShardUndoFn)(void* user);
/* The user opened (double click, "Open") the asset at `path` in the project, of an extension the plugin registered with add_asset_opener */
typedef void (SHARD_CALL *ShardOpenAssetFn)(void* user, ShardStr path);

#define SHARD_EDITOR_NOTIFY_INFO     0
#define SHARD_EDITOR_NOTIFY_WARNING  1
#define SHARD_EDITOR_NOTIFY_ERROR    2

typedef struct ShardEditorApi {
    uint32_t structSize;

    /* Adds a panel named `title` to the editor's Window menu, from on_load. It goes with the plugin. thread : main.
     * SHARD_E_ALREADY_EXISTS : this plugin has a panel of that name. */
    ShardResult (SHARD_CALL *add_panel)(ShardPluginCtx ctx, ShardStr title, ShardPanelFn draw, void* user);

    /* ---- 1.1 : everything below goes with the plugin, thread : main, from on_load ---- */
    /* An item of a menu of the menu bar. `path` is the menu and the item, separated by '/' ("Tools/Bake/All levels") ; the first part
     * is a menu of the menu bar ("Tools" is added when a plugin uses it). `shortcut` is only displayed ("Ctrl+B", or empty).
     * SHARD_E_ALREADY_EXISTS : this plugin has an item at that path. */
    ShardResult (SHARD_CALL *add_menu_item)(ShardPluginCtx ctx, ShardStr path, ShardStr shortcut, ShardEditorActionFn run, void* user);
    /* A button in the viewport's toolbar. SHARD_E_ALREADY_EXISTS : this plugin has one of that label. */
    ShardResult (SHARD_CALL *add_toolbar_button)(ShardPluginCtx ctx, ShardStr label, ShardStr tooltip, ShardEditorActionFn run, void* user);
    /* Draws below the component `component_type` ("light", "physics_body", "<plugin id>.<name>"...) of the actor the Properties panel shows ;
     * an empty type : below all the components, for any actor. */
    ShardResult (SHARD_CALL *add_inspector)(ShardPluginCtx ctx, ShardStr component_type, ShardInspectorFn draw, void* user);
    /* Draws in the viewport for every selected actor. SHARD_E_ALREADY_EXISTS : this plugin has a gizmo of that name. */
    ShardResult (SHARD_CALL *add_gizmo)(ShardPluginCtx ctx, ShardStr name, ShardGizmoFn draw, void* user);
    /* Tells when the selection changes. */
    ShardResult (SHARD_CALL *subscribe_selection)(ShardPluginCtx ctx, ShardSelectionFn fn, void* user);

    /* The selected actors, as an "array out" : up to `capacity` into `out`, how many there are in *count. thread : main */
    ShardResult (SHARD_CALL *get_selection)(ShardActor* out, uint32_t capacity, uint32_t* count);
    /* A notification in the corner of the editor (SHARD_EDITOR_NOTIFY_*). thread : main */
    ShardResult (SHARD_CALL *notify)(ShardPluginCtx ctx, uint32_t level, ShardStr message);
    /* Shows or hides one of this plugin's panels. SHARD_E_NOT_FOUND : no such panel. thread : main */
    ShardResult (SHARD_CALL *set_panel_visible)(ShardPluginCtx ctx, ShardStr title, int32_t visible);

    /* ---- 1.2 : thread : main ---- */
    /* Records a change the plugin just made (a node moved, a wire cut) in the editor's undo history, under `label`. undo puts it back, redo
     * does it again ; the editor's Ctrl+Z / Ctrl+Y call them in order with the edits of the editor itself. The two are called with `user`
     * and may run after the panel closed : keep what they need alive (the history is dropped when the plugin is disabled). */
    ShardResult (SHARD_CALL *undo_push)(ShardPluginCtx ctx, ShardStr label, ShardUndoFn undo, ShardUndoFn redo, void* user);
    /* Opens assets of that extension (".bt") in the plugin : a double click in the content browser calls `open`, which usually makes a panel
     * visible and loads the file. SHARD_E_ALREADY_EXISTS : the engine or another plugin opens that extension. */
    ShardResult (SHARD_CALL *add_asset_opener)(ShardPluginCtx ctx, ShardStr extension, ShardOpenAssetFn open, void* user);
    /* An entry "label" of the content browser's Create menu that writes a new file with these bytes (copied) under `default_name`
     * ("NewTree.bt") in the folder shown. The file is renamed by the user like any other. */
    ShardResult (SHARD_CALL *add_asset_template)(ShardPluginCtx ctx, ShardStr label, ShardStr default_name, const void* data, uint64_t size);
} ShardEditorApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_EDITOR_H */
