#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// TYPE DEFINITIONS & ENUMS
// ============================================================================

typedef uint64_t ui_handle_t;
typedef uint32_t ui_color_t; // 0xRRGGBBAA

enum UiNodeType {
    UI_NODE_FLEX_BOX = 0,
    UI_NODE_TEXT = 1,
    UI_NODE_IMAGE = 2,
    UI_NODE_SVG = 3,
    UI_NODE_SCROLLVIEW = 4,
    UI_NODE_GRID = 5,
    UI_NODE_PATH = 6 // For dynamic vector shapes (charts, waveforms)
};

enum UiEvent {
    UI_EVENT_POINTER_DOWN = 1,
    UI_EVENT_POINTER_UP = 2,
    UI_EVENT_POINTER_MOVE = 3,
    UI_EVENT_POINTER_ENTER = 4,
    UI_EVENT_POINTER_LEAVE = 5,
    UI_EVENT_CLICK = 6,
    UI_EVENT_RIGHT_CLICK = 7
};

enum UiKeyEventType {
    UI_KEY_EVENT_DOWN = 1,
    UI_KEY_EVENT_UP = 2
};

enum UiKeyModifier {
    UI_KEY_MOD_SHIFT = 1 << 0,
    UI_KEY_MOD_CTRL = 1 << 1,
    UI_KEY_MOD_ALT = 1 << 2,
    UI_KEY_MOD_META = 1 << 3
};

enum UiSizeUnit {
    UI_SIZE_UNIT_PIXEL = 0,
    UI_SIZE_UNIT_AUTO = 1,
    UI_SIZE_UNIT_PERCENT = 2
};

enum UiGridUnit {
    UI_GRID_UNIT_PIXEL = 0,
    UI_GRID_UNIT_AUTO = 1,
    UI_GRID_UNIT_STAR = 2
};

enum UiSemanticRole {
    UI_SEMANTIC_NONE = 0,
    UI_SEMANTIC_BUTTON = 1,
    UI_SEMANTIC_TEXTBOX = 2,
    UI_SEMANTIC_LINK = 3,
    UI_SEMANTIC_HEADING = 4,
    UI_SEMANTIC_FORM = 5,
    UI_SEMANTIC_LIST = 6,
    UI_SEMANTIC_LIST_ITEM = 7,
    UI_SEMANTIC_IMAGE = 8,
    UI_SEMANTIC_DIALOG = 9
};

// ============================================================================
// SURFACE A: TIER 3 (ASSEMBLY-SCRIPT) -> TIER 2 (C++)
// Called synchronously by the User App on the UI Web Worker.
// Exact declarations live in `v2/ui/include/effindom_ui.h`; the grouped list
// below documents the shipped ABI families and key semantics.
// ============================================================================

// --- 1. Memory Management ---
uint32_t ui_arena_alloc(uint32_t size);

// --- 2. Node Lifecycle & Identity ---
ui_handle_t ui_create_node(uint32_t type);
void ui_delete_node(ui_handle_t handle);

// Used for transient state snapshots (scroll restoration, semantic anchors).
// IDs are unique across all active nodes. Reassigning the same ID to the same
// node is a no-op; assigning an active ID to a different node is rejected.
// AS must encode the ID to UTF-8 and pass it via the Frame Arena.
void ui_set_node_id(ui_handle_t handle, const uint8_t* utf8_id, uint32_t len);
void ui_set_semantic_role(ui_handle_t handle, uint32_t role_enum);
void ui_set_semantic_label(ui_handle_t handle, const uint8_t* utf8_label, uint32_t len);

// --- 3. Tree Hierarchy & Portals ---
void ui_node_add_child(ui_handle_t parent, ui_handle_t child);
void ui_node_remove_child(ui_handle_t parent, ui_handle_t child);
// Portal roots still render in normal DFS order, but their children are deferred
// to the tail of the retained paint order so they render and hit-test on top.
void ui_set_is_portal(ui_handle_t handle, bool is_portal);

// --- 4. Layout (Yoga Flexbox) ---
void ui_set_width(ui_handle_t handle, float value, uint32_t unit_enum);
void ui_set_height(ui_handle_t handle, float value, uint32_t unit_enum);
void ui_set_flex_direction(ui_handle_t handle, uint32_t dir_enum);
void ui_set_flex_grow(ui_handle_t handle, float grow);
void ui_set_flex_basis(ui_handle_t handle, float basis);
void ui_set_justify_content(ui_handle_t handle, uint32_t justify_enum);
void ui_set_align_items(ui_handle_t handle, uint32_t align_enum);
void ui_set_padding(ui_handle_t handle, float left, float top, float right, float bottom);
void ui_set_position_type(ui_handle_t handle, uint32_t pos_enum);
void ui_set_position(ui_handle_t handle, float left, float top, float right, float bottom);

// --- 5. Layout (WPF Grid) ---
// `UI_NODE_GRID` runs a retained three-pass layout in Tier 2: unconstrained child
// measurement, track sizing for Pixel/Auto/Star rows and columns, then final child
// arrangement into cell-constrained Yoga layouts. Column and row spanning are applied
// during track sizing and arrangement; wrapped text is remeasured against the final
// column width before auto row heights are resolved. WPF-style shared sizing is also
// supported: any node can be marked as a shared-size scope, grids inside that scope
// can opt individual rows/columns into named shared groups, and Star tracks that join
// a shared group are treated as Auto for the shared measurement pass. Tier 2 stores
// that shared-size metadata in Grid-owned runtime side tables keyed by handles rather
// than on `UINode` itself.
void ui_set_is_shared_size_scope(ui_handle_t handle, bool is_scope);
void ui_grid_set_columns(ui_handle_t handle, uint32_t count, const float* values, const uint8_t* types);
void ui_grid_set_rows(ui_handle_t handle, uint32_t count, const float* values, const uint8_t* types);
void ui_grid_set_column_shared_size_group(ui_handle_t handle, uint32_t index, const uint8_t* utf8_group, uint32_t len);
void ui_grid_set_row_shared_size_group(ui_handle_t handle, uint32_t index, const uint8_t* utf8_group, uint32_t len);
void ui_node_set_grid_placement(ui_handle_t child, uint32_t row, uint32_t col, uint32_t row_span, uint32_t col_span);

// --- 6. Visuals & Styling ---
void ui_set_bg_color(ui_handle_t handle, ui_color_t color);
// Combined retained box-style update used by the current FUI-AS styling surface.
// Radius and border fields live in the same retained node state and commit as one command.
void ui_set_box_style(
    ui_handle_t handle,
    ui_color_t bg_color,
    float radius_tl,
    float radius_tr,
    float radius_br,
    float radius_bl,
    float border_width,
    ui_color_t border_color,
    uint32_t border_style_enum,
    float border_dash_on,
    float border_dash_off);
// Tier 2 emits retained `OP_PUSH_CLIP` / `OP_POP` scene ops for this flag. `UI_NODE_SCROLLVIEW`
// also clips its descendants by default so viewport scrolling does not leak content outside bounds.
// In v2 FUI-AS, FlexBox-derived containers now default to clipping too; callers opt back out with
// `clipToBounds(false)` when overflow-visible behavior is intentional. `Portal`
// remains the built-in overflow-visible escape hatch and opts out of clipping by
// default so dropdown/dialog/context-menu overlays can escape their anchor bounds.
void ui_set_clip_to_bounds(ui_handle_t handle, bool clip);
void ui_set_linear_gradient(ui_handle_t handle, float sx, float sy, float ex, float ey, uint32_t stop_count, const float* offsets, const ui_color_t* colors);
void ui_set_drop_shadow(ui_handle_t handle, ui_color_t color, float offset_x, float offset_y, float blur_sigma, float spread);
void ui_set_layer_effect(ui_handle_t handle, float opacity, float blur_sigma, uint32_t blend_mode_enum);
void ui_set_background_blur(ui_handle_t handle, float blur_sigma);

// --- 7. Media, Assets & Paths ---
// These setters are now implemented in the retained runtime: `UI_NODE_IMAGE` and
// `UI_NODE_SVG` store retained media state in Tier 2, and frame commit emits
// `CMD_SET_IMAGE`, `CMD_SET_IMAGE_NINE`, and `CMD_SET_SVG` into the Tier 1
// command buffer when those nodes are dirty.
void ui_set_image(ui_handle_t handle, uint32_t texture_id, uint32_t object_fit_enum);
void ui_set_image_nine(ui_handle_t handle, uint32_t texture_id, float inset_l, float inset_t, float inset_r, float inset_b);
void ui_set_svg(ui_handle_t handle, uint32_t svg_id, ui_color_t tint_color);

// --- 8. Typography (HarfBuzz) ---
void ui_set_text(ui_handle_t handle, const uint8_t* utf8_str, uint32_t len);
void ui_set_font(ui_handle_t handle, uint32_t font_id, float size);
// Uses a fixed pixel line box when `line_height > 0`; `<= 0` restores the
// default font-driven `normal` path. Explicit line height uses the node's
// primary-font strut for every visual line instead of letting fallback glyphs
// expand individual rows.
void ui_set_line_height(ui_handle_t handle, float line_height);
void ui_register_font_fallback(uint32_t font_id, uint32_t fallback_font_id);
void ui_set_text_color(ui_handle_t handle, ui_color_t color);
void ui_set_text_align(ui_handle_t handle, uint32_t align_enum);
// Offsets the wrapped paragraph block within the allocated text node height. Caret,
// hit-testing, and selection highlight geometry use the same retained vertical offset.
void ui_set_text_vertical_align(ui_handle_t handle, uint32_t align_enum);
void ui_set_text_limits(ui_handle_t handle, int32_t max_chars, int32_t max_lines);
void ui_set_text_wrapping(ui_handle_t handle, bool wrap);
void ui_set_text_overflow(ui_handle_t handle, uint32_t overflow_enum);
// Masks emitted glyph IDs to the registered font's bullet glyph while keeping
// the underlying UTF-8 text, shaping, and layout measurements unchanged.
void ui_set_text_obscured(ui_handle_t handle, bool is_password);

// Caller must ensure the string is in the Frame Arena and remains valid until after the call. 
// Returns logical width and height.
void ui_measure_text(const uint8_t* utf8_str, uint32_t len, uint32_t font_id, float size, float max_width, float* out_width, float* out_height);

// --- 9. Interaction & State ---
void ui_set_interactive(ui_handle_t handle, bool interactive);
void ui_set_focusable(ui_handle_t handle, bool focusable, int32_t tab_index);
void ui_request_focus(ui_handle_t handle);
// For `UI_NODE_SCROLLVIEW`, Tier 2 clamps the requested viewport offset against Yoga-derived
// content bounds during commit, clips descendants to the viewport, and emits `as_on_scroll()`
// whenever the effective offset changes.
void ui_set_scroll_offset(ui_handle_t handle, float offset_x, float offset_y);
void ui_set_selectable(ui_handle_t handle, bool selectable, ui_color_t selection_color);
// Marks any container as a cross-node text-selection region. During pointer drags and
// Shift-based keyboard extension, Tier 2 flattens selectable descendant text nodes in
// DFS order and emits temporary highlight overrides plus stitched clipboard text. Collapsed
// ranges still show the normal caret, and scroll-view auto-scroll keeps updating the range
// endpoint as new text moves under the pointer.
void ui_set_selection_area(ui_handle_t handle, bool is_area);
void ui_set_selection_area_barrier(ui_handle_t handle, bool is_barrier);
void ui_clear_selection(ui_handle_t text_node_handle);
void ui_retarget_selection(ui_handle_t from_text_node_handle, ui_handle_t to_text_node_handle);
bool ui_is_point_in_selection(float logical_x, float logical_y);
void ui_set_text_selection_range(ui_handle_t handle, uint32_t selection_start, uint32_t selection_end);
void ui_set_editable(ui_handle_t handle, bool editable);
void ui_set_caret_color(ui_handle_t handle, ui_color_t color);

// --- 10. The Frame Trigger ---
void ui_commit_frame();


// ============================================================================
// SURFACE B: JS BRIDGE -> TIER 2 (C++)
// Called by the JS Bridge to route OS events and extract buffers.
// ============================================================================

// --- 1. Hardware Events ---
void ui_set_key_modifiers(uint32_t modifiers);
void ui_on_pointer_event(uint32_t event_enum, ui_handle_t handle, float logical_x, float logical_y);
void ui_on_wheel_event(float delta_x, float delta_y);
void ui_touch_scroll_begin(ui_handle_t handle, float logical_x, float logical_y);
void ui_touch_scroll_update(float delta_x, float delta_y);
void ui_touch_scroll_end(void);
void ui_on_key_event(uint32_t type_enum, const uint8_t* key_utf8, uint32_t len, uint32_t modifiers);
void ui_set_interaction_time(uint64_t interaction_time_ms);
void ui_on_ime_update(ui_handle_t handle, const uint8_t* utf8_str, uint32_t len, uint32_t caret_idx);
void ui_replace_text_range(ui_handle_t handle, uint32_t start_idx, uint32_t end_idx, const uint8_t* utf8_str, uint32_t len, uint32_t caret_idx);
void ui_on_paste_text(ui_handle_t handle, const uint8_t* utf8_str, uint32_t len);

// --- 2. Environment ---
void ui_resize_window(float logical_w, float logical_h);
void ui_register_font(uint32_t font_id, const uint8_t* bytes, uint32_t len);
void ui_register_font_fallback(uint32_t font_id, uint32_t fallback_font_id);

// --- 3. Buffer Extraction ---
const uint32_t* ui_get_command_buffer(uint32_t* out_length);
// Packed as [RecordCount, Role, HandleL, HandleU, X, Y, W, H, LabelByteLength, LabelWords...].
// Labels are UTF-8 and padded to a 32-bit boundary; traversal order follows retained paint order,
// including deferred portal children.
const uint32_t* ui_get_semantic_buffer(uint32_t* out_length);
bool ui_get_bounds(ui_handle_t handle, float* out_x, float* out_y, float* out_width, float* out_height);


// ============================================================================
// SURFACE C: TIER 2 (C++) -> TIER 3 (ASSEMBLY-SCRIPT)
// CALLBACKS. Tier 2 expects Tier 3 to export these functions.
// ============================================================================

extern void as_on_focus_changed(ui_handle_t handle, bool is_focused);
extern void as_on_pointer_event(ui_handle_t handle, uint32_t event_enum);
extern void as_on_text_changed(ui_handle_t handle, const uint8_t* utf8_str, uint32_t len);
extern void as_on_text_replaced(ui_handle_t handle, uint32_t start_idx, uint32_t end_idx, const uint8_t* utf8_str, uint32_t len);
// Tier 2 calls this for programmatic scrolls, drag scrolling, edge-trigger auto-scroll, and momentum ticks.
extern void as_on_scroll(
    ui_handle_t handle,
    float offset_x,
    float offset_y,
    float content_width,
    float content_height,
    float viewport_width,
    float viewport_height);
extern void as_on_selection_changed(ui_handle_t handle, uint32_t start_idx, uint32_t end_idx);
// Fired whenever a selection-area range changes. The stitched UTF-8 payload spans all
// covered selectable text descendants, using newlines for vertical/block siblings and
// spaces for horizontal siblings.
extern void as_on_cross_selection_changed(ui_handle_t area_handle, const uint8_t* utf8_str, uint32_t len);
extern void as_on_clipboard_write(
    const uint8_t* plain_utf8_str,
    uint32_t plain_len,
    const uint8_t* rich_json_utf8_str,
    uint32_t rich_json_len);
extern void as_on_request_clipboard_read(ui_handle_t handle);

#ifdef __cplusplus
}
#endif

`ui_register_font_fallback(...)` attaches fallback faces to an existing primary `font_id`. Text nodes still store that primary `font_id`, but retained glyph runs can now mix faces after shaping. The command buffer keeps the existing glyph-run header (`FontID`, `FontSize`, `Color`, `GlyphCount`) and extends each glyph tuple from `(GlyphID, X, Y)` to `(GlyphID, X, Y, FontID)` so Tier 1 renders fallback-selected glyphs with the correct font face.

Clipboard copy now follows the same split: Tier 2 emits plain UTF-8 plus an
optional rich-selection JSON payload through `as_on_clipboard_write(...)`, and
the browser bridge decides how that becomes `text/plain`, `text/html`, and the
custom web clipboard format.
